#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <cstring>
#include <algorithm>
#include <utility>

#include "MarkdownStream.h"
#include "nativeapps/notes/NotesDocument.h"

namespace ybxnote {

// Conserve C3 memory and refuse potentially expensive unbounded migrations.
// Result is an IN-MEMORY view only: original clipping IDs, offsets, revision,
// sync outbox and tombstones are deliberately never rewritten.
inline constexpr size_t kMaxRelocations = 16;
inline constexpr size_t kMaxQuoteBytes = 512;
inline constexpr size_t kMinQuoteBytes = 8;
inline constexpr size_t kMaxVisibleBytes = nativeapps::notes::kMaxFileBytes * 2 + 64;

struct Relocation {
  uint16_t index = 0;
  uint32_t start = 0; // Codepoint index in the EXACT renderer's XHTML body text stream
  uint32_t end = 0;   // Exclusive
};

enum class Match : uint8_t { Found, Missing, Ambiguous, Unsupported };

struct Candidate {
  Match status = Match::Unsupported;
  uint32_t start = 0;
  uint32_t end = 0;
};

// MarkdownStream generates only these three named entities. The text node
// offsets match the ChapterHtmlSlimParser's characterData codepoint counter,
// including newlines between structural tags. We consume the SAME emitter as
// TxtToHtml, but never keep the possibly very large escaped XHTML in RAM.
class VisibleText {
 public:
  void initialBodyNewline() { append('\n'); }
  void finalBodyNewline() { append('\n'); }

  void feed(std::string_view fragment) {
    for (const char c : fragment) {
      if (!valid_) return;
      if (mode_ == Mode::Tag) {
        if (c == '>') mode_ = Mode::Text;
      } else if (mode_ == Mode::Entity) {
        if (c == ';') {
          if (entity_ == "amp") append('&');
          else if (entity_ == "lt") append('<');
          else if (entity_ == "gt") append('>');
          else valid_ = false; // never trust an unrecognised entity
          entity_.clear();
          mode_ = Mode::Text;
        } else if (entity_.size() < 8 && c != '<' && c != '&') {
          entity_.push_back(c);
        } else {
          valid_ = false;
        }
      } else if (c == '<') {
        mode_ = Mode::Tag;
      } else if (c == '&') {
        mode_ = Mode::Entity;
      } else {
        append(c);
      }
    }
  }

  bool finish(std::string& result) {
    if (!valid_ || mode_ != Mode::Text || text_.size() > kMaxVisibleBytes ||
        !nativeapps::notes::isValidUtf8(text_, true)) return false;
    result = std::move(text_);
    return true;
  }

 private:
  enum class Mode : uint8_t { Text, Tag, Entity };
  Mode mode_ = Mode::Text;
  bool valid_ = true;
  std::string entity_;
  std::string text_;

  void append(char c) {
    if (text_.size() >= kMaxVisibleBytes) { valid_ = false; return; }
    text_.push_back(c);
  }
};

inline bool renderVisibleText(std::string_view source, std::string& visible) {
  // A malformed/copied file which only resembles a generated Native Note
  // must not be used to invent source coordinates.
  nativeapps::notes::Document note;
  if (source.size() > nativeapps::notes::kMaxFileBytes ||
      !nativeapps::notes::decode(source, note)) return false;

  struct View { std::string_view bytes; size_t offset = 0; } input{source};
  VisibleText sink;
  sink.initialBodyNewline(); // TxtToHtml writes "<body>\n".
  auto output = [&sink](std::string_view fragment) { sink.feed(fragment); };
  auto ignoreHeading = [](unsigned, uint8_t, const std::string&) {};
  const bool ok = MarkdownStream::process(
      &input, [](void* ctx, uint8_t* buf, size_t size) -> int {
        auto* v = static_cast<View*>(ctx);
        const size_t count = std::min(size, v->bytes.size() - v->offset);
        if (!count) return 0;
        std::memcpy(buf, v->bytes.data() + v->offset, count);
        v->offset += count;
        return static_cast<int>(count);
      }, output, ignoreHeading, true);
  sink.finalBodyNewline(); // TxtToHtml appends "\n</body>".
  return ok && sink.finish(visible);
}

inline uint32_t codepointCount(std::string_view utf8) {
  uint32_t count = 0;
  for (const unsigned char c : utf8)
    if ((c & 0xC0) != 0x80) ++count;
  return count;
}

inline bool wordish(unsigned char c) {
  return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

// Only exact, unique, whole-token occurrences are safe. In particular,
// selecting one occurrence of a phrase repeated anywhere else in the note
// never selects an arbitrary first or nearest match.
inline Candidate findUnique(std::string_view visible, std::string_view quote) {
  if (quote.size() < kMinQuoteBytes || quote.size() > kMaxQuoteBytes ||
      visible.size() > kMaxVisibleBytes || quote.front() <= ' ' || quote.back() <= ' ' ||
      !nativeapps::notes::isValidUtf8(quote, true) ||
      !nativeapps::notes::isValidUtf8(visible, true)) return {};

  Candidate result{Match::Missing};
  for (size_t at = 0; at < visible.size();) {
    at = visible.find(quote, at);
    if (at == std::string_view::npos) break;
    const size_t end = at + quote.size();
    // No hits inside a word, nor inside a multi-byte UTF-8 codepoint.
    const bool leftBoundary = at == 0 || !wordish(static_cast<unsigned char>(visible[at - 1])) ||
                              !wordish(static_cast<unsigned char>(quote.front()));
    const bool rightBoundary = end == visible.size() ||
                               !wordish(static_cast<unsigned char>(visible[end])) ||
                               !wordish(static_cast<unsigned char>(quote.back()));
    if (leftBoundary && rightBoundary &&
        (static_cast<unsigned char>(visible[at]) & 0xC0) != 0x80 &&
        (end == visible.size() || (static_cast<unsigned char>(visible[end]) & 0xC0) != 0x80)) {
      if (result.status == Match::Found) return {Match::Ambiguous};
      result = {Match::Found, codepointCount(visible.substr(0, at)), 0};
      result.end = result.start + codepointCount(quote);
    }
    ++at; // detect overlapping duplicates too
  }
  return result;
}

inline bool contains(const Relocation& item, uint16_t index, uint32_t start, uint32_t end) {
  return item.index == index && start != UINT32_MAX && end != UINT32_MAX &&
         end > item.start && start < item.end;
}

} // namespace ybxnote
