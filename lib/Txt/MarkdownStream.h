#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// Deliberately small, streaming, original Markdown subset for low-memory
// e-readers. Used identically by HTML rendering and TOC discovery so heading
// anchors are stable. No raw HTML, remote resource fetch or custom scripts.
// Input is treated as UTF-8 text; downstream reader applies normal font rules.
namespace MarkdownStream {

inline constexpr size_t kMaxLineBytes = 2048;
inline constexpr unsigned kMaxTocHeadings = 128;

inline std::string anchorFor(unsigned ordinal) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "md-h%04u", ordinal);
  return buf;
}

inline std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

inline int headingLevel(std::string_view line, std::string_view& text) {
  line = trim(line);
  size_t n = 0;
  while (n < line.size() && n < 6 && line[n] == '#') ++n;
  if (n == 0 || n >= line.size() || (line[n] != ' ' && line[n] != '\t')) return 0;
  text = trim(line.substr(n + 1));
  // Markdown's optional closing hashes are formatting, not heading text.
  size_t end = text.size();
  while (end > 0 && text[end - 1] == '#') --end;
  if (end < text.size() && end > 0 && text[end - 1] == ' ') text = trim(text.substr(0, end));
  if (text.empty()) return 0;
  return static_cast<int>(n);
}

inline std::string headingTitle(std::string_view source) {
  // Strip simple inline formatting markers from TOC labels, with a strict
  // byte cap and no UTF-8 codepoint truncation.
  std::string label;
  label.reserve(source.size() < 128 ? source.size() : 128);
  for (size_t i = 0; i < source.size() && label.size() < 112; ++i) {
    const char ch = source[i];
    if (ch == '*' || ch == '`') continue;
    if (ch == '[' || ch == ']') continue;
    if (ch == '(' && i > 0 && source[i - 1] == ']') {
      size_t end = source.find(')', i + 1);
      if (end != std::string_view::npos) {
        i = end;
        continue;
      }
    }
    label.push_back(ch);
  }
  // If a label was truncated, cut back to last complete UTF-8 sequence.
  if (label.size() == 112) {
    while (!label.empty() && (static_cast<unsigned char>(label.back()) & 0xC0) == 0x80) label.pop_back();
    if (!label.empty() && static_cast<unsigned char>(label.back()) >= 0xC0) label.pop_back();
  }
  return std::string(trim(label));
}

template <typename Append, typename OnHeading>
class Parser {
 public:
  Parser(Append& output, OnHeading& heading, const bool render)
      : output_(output), onHeading_(heading), render_(render) {
    line_.reserve(160);
  }

  // Never pass malformed UTF-8 or NUL/control bytes to Expat. Validate
  // multibyte sequences across read chunks and replace invalid sequences
  // with '?', keeping the same heading IDs in the TOC scan and HTML.
  void feed(std::string_view bytes) {
    for (const unsigned char c : bytes) {
      if (pendingLength_ != 0) {
        if ((c & 0xC0) == 0x80) {
          pending_[pendingLength_++] = static_cast<char>(c);
          if (pendingLength_ == pendingExpected_) {
            const unsigned char first = static_cast<unsigned char>(pending_[0]);
            const unsigned char second = static_cast<unsigned char>(pending_[1]);
            const bool valid = !(first == 0xE0 && second < 0xA0) && !(first == 0xED && second >= 0xA0) &&
                               !(first == 0xF0 && second < 0x90) && !(first == 0xF4 && second >= 0x90);
            if (valid) {
              for (unsigned n = 0; n < pendingLength_; ++n) consume(pending_[n]);
            } else {
              consume('?');
            }
            pendingLength_ = 0;
          }
          continue;
        }
        // A bad continuation cannot swallow the next valid ASCII or lead.
        consume('?');
        pendingLength_ = 0;
      }
      if (c < 0x80) {
        consume(c == 0 || (c < 0x20 && c != '\r' && c != '\n' && c != '\t') ? '?' : static_cast<char>(c));
      } else if (c >= 0xC2 && c <= 0xF4) {
        pending_[0] = static_cast<char>(c);
        pendingLength_ = 1;
        pendingExpected_ = c <= 0xDF ? 2 : c <= 0xEF ? 3 : 4;
      } else {
        consume('?');
      }
    }
  }

  void consume(const char c) {
    if (c == '\r') return;
    if (c == '\n') {
      flushLine();
      return;
    }
    if (overflow_) {
      if (render_) escape(std::string_view(&c, 1));
      return;
    }
    if (line_.size() == kMaxLineBytes) {
      overflow_ = true;
      if (render_) {
        if (!code_) {
          endBlock();
          put("<p>");
        }
        escape(line_);
      }
      line_.clear();
      if (render_) escape(std::string_view(&c, 1));
      return;
    }
    line_.push_back(c);
  }

  void finish() {
    if (pendingLength_ != 0) {
      consume('?');
      pendingLength_ = 0;
    }
    if (overflow_) {
      if (render_) put(code_ ? "\n" : "</p>\n");
      overflow_ = false;
      line_.clear();
    } else if (!line_.empty()) {
      flushLine();
    }
    if (code_) {
      put("</code></pre>\n");
      code_ = false;
    }
    if (render_) endBlock();
  }

 private:
  void put(std::string_view s) {
    if (render_) output_(s);
  }
  void escape(std::string_view s) {
    for (const char c : s) {
      switch (c) {
        case '&':
          put("&amp;");
          break;
        case '<':
          put("&lt;");
          break;
        case '>':
          put("&gt;");
          break;
        default:
          put(std::string_view(&c, 1));
          break;
      }
    }
  }

  void inlineText(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
      const char ch = text[i];
      if (ch == '`') {
        const size_t end = text.find('`', i + 1);
        if (end != std::string_view::npos && end > i + 1) {
          put("<code>");
          escape(text.substr(i + 1, end - i - 1));
          put("</code>");
          i = end + 1;
          continue;
        }
      }
      if (ch == '*' && i + 1 < text.size() && text[i + 1] == '*') {
        const size_t end = text.find("**", i + 2);
        if (end != std::string_view::npos && end > i + 2) {
          put("<strong>");
          escape(text.substr(i + 2, end - i - 2));
          put("</strong>");
          i = end + 2;
          continue;
        }
      }
      if ((ch == '*' || ch == '_') && i + 1 < text.size()) {
        const size_t end = text.find(ch, i + 1);
        if (end != std::string_view::npos && end > i + 1) {
          put("<em>");
          escape(text.substr(i + 1, end - i - 1));
          put("</em>");
          i = end + 1;
          continue;
        }
      }
      if (ch == '[') {
        const size_t labelEnd = text.find(']', i + 1);
        if (labelEnd != std::string_view::npos && labelEnd + 1 < text.size() && text[labelEnd + 1] == '(') {
          const size_t hrefEnd = text.find(')', labelEnd + 2);
          if (hrefEnd != std::string_view::npos) {
            // Do not create actionable remote URL links from untrusted local
            // Markdown. Show the label and destination as readable text.
            escape(text.substr(i + 1, labelEnd - i - 1));
            const auto href = text.substr(labelEnd + 2, hrefEnd - labelEnd - 2);
            if (!href.empty()) {
              put(" (");
              escape(href);
              put(")");
            }
            i = hrefEnd + 1;
            continue;
          }
        }
      }
      escape(text.substr(i, 1));
      ++i;
    }
  }

  void endBlock() {
    if (paragraph_) {
      put("</p>\n");
      paragraph_ = false;
    }
    if (list_ != 0) {
      put(list_ == 1 ? "</ul>\n" : "</ol>\n");
      list_ = 0;
    }
  }

  static bool bullet(std::string_view s, unsigned& type, std::string_view& text) {
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '+' || s[0] == '*') && s[1] == ' ') {
      type = 1;
      text = trim(s.substr(2));
      return true;
    }
    size_t digits = 0;
    while (digits < s.size() && digits < 5 && s[digits] >= '0' && s[digits] <= '9') ++digits;
    if (digits > 0 && digits + 1 < s.size() && (s[digits] == '.' || s[digits] == ')') && s[digits + 1] == ' ') {
      type = 2;
      text = trim(s.substr(digits + 2));
      return true;
    }
    return false;
  }

  void flushLine() {
    std::string_view text = line_;
    if (firstLine_) {
      firstLine_ = false;
      if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    }
    if (overflow_) {
      if (render_) put(code_ ? "\n" : "</p>\n");
      overflow_ = false;
      line_.clear();
      return;
    }
    const std::string_view stripped = trim(text);
    if (code_) {
      if (stripped.size() >= 3 && stripped[0] == fence_ && stripped[1] == fence_ && stripped[2] == fence_) {
        put("</code></pre>\n");
        code_ = false;
      } else {
        escape(text);
        put("\n");
      }
      line_.clear();
      return;
    }
    if (stripped.size() >= 3 && (stripped.substr(0, 3) == "```" || stripped.substr(0, 3) == "~~~")) {
      if (render_) endBlock();
      fence_ = stripped[0];
      code_ = true;
      put("<pre><code>");
      line_.clear();
      return;
    }
    if (stripped.empty()) {
      if (render_) endBlock();
      line_.clear();
      return;
    }
    std::string_view heading;
    const int level = headingLevel(stripped, heading);
    if (level) {
      if (render_) endBlock();
      ++headingCount_;
      if (headingCount_ <= kMaxTocHeadings)
        onHeading_(headingCount_, static_cast<uint8_t>(level), headingTitle(heading));
      if (render_) {
        const char digit = static_cast<char>('0' + level);
        put("<h");
        put(std::string_view(&digit, 1));
        if (headingCount_ <= kMaxTocHeadings) {
          put(" id=\"");
          put(anchorFor(headingCount_));
          put("\"");
        }
        put(">");
        inlineText(heading);
        put("</h");
        put(std::string_view(&digit, 1));
        put(">\n");
      }
      line_.clear();
      return;
    }
    unsigned listType = 0;
    std::string_view item;
    if (bullet(stripped, listType, item)) {
      if (render_) {
        if (paragraph_) {
          put("</p>\n");
          paragraph_ = false;
        }
        if (list_ && list_ != listType) endBlock();
        if (!list_) {
          list_ = listType;
          put(list_ == 1 ? "<ul>\n" : "<ol>\n");
        }
        put("<li>");
        inlineText(item);
        put("</li>\n");
      }
      line_.clear();
      return;
    }
    if (stripped.size() > 1 && stripped[0] == '>' && stripped[1] == ' ') {
      if (render_) {
        endBlock();
        put("<blockquote><p>");
        inlineText(stripped.substr(2));
        put("</p></blockquote>\n");
      }
      line_.clear();
      return;
    }
    if (render_) {
      if (list_) endBlock();
      if (!paragraph_) {
        put("<p>");
        paragraph_ = true;
      } else
        put(" ");
      inlineText(stripped);
    }
    line_.clear();
  }

  Append& output_;
  OnHeading& onHeading_;
  std::string line_;
  unsigned headingCount_ = 0;
  unsigned list_ = 0;
  char fence_ = 0;
  bool firstLine_ = true;
  bool code_ = false;
  bool paragraph_ = false;
  bool overflow_ = false;
  bool render_ = true;
  char pending_[4] = {};
  unsigned pendingLength_ = 0;
  unsigned pendingExpected_ = 0;
};

template <typename Append, typename Heading>
bool process(void* context, int (*read)(void*, uint8_t*, size_t), Append& output, Heading& onHeading, bool render) {
  Parser<Append, Heading> parser(output, onHeading, render);
  uint8_t chunk[512];
  int size;
  while ((size = read(context, chunk, sizeof(chunk))) > 0) {
    parser.feed(std::string_view(reinterpret_cast<const char*>(chunk), static_cast<size_t>(size)));
  }
  if (size < 0) return false;
  parser.finish();
  return true;
}

}  // namespace MarkdownStream
