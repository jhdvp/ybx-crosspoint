#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace nativeapps::notes {

// Bounded portable Markdown format. Files contain a single H1 and body; they
// can be opened by the existing YBX Markdown/EPUB reader and transferred via
// USB without translating a proprietary note database.
inline constexpr size_t kMaxNotes = 32;
inline constexpr size_t kMaxTitleBytes = 96;
inline constexpr size_t kMaxBodyBytes = 4096;
inline constexpr size_t kMaxFileBytes = kMaxTitleBytes + kMaxBodyBytes + 5;
inline constexpr size_t kFileNameBytes = 24;  // note- + 16 hex + .md
inline constexpr const char* kDirectory = "/apps/notes";

struct Document {
  std::string title;
  std::string body;
};

inline bool isValidUtf8(std::string_view value, bool allowNewlines) {
  for (size_t i = 0; i < value.size();) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c == 0 || c == 0x7F || (c < 0x20 && c != '\t' && (!allowNewlines || (c != '\n' && c != '\r'))))
      return false;
    if (c < 0x80) {
      ++i;
      continue;
    }
    const int length = (c >= 0xC2 && c <= 0xDF) ? 2
                       : (c >= 0xE0 && c <= 0xEF) ? 3
                       : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if (length == 0 || i + static_cast<size_t>(length) > value.size()) return false;
    const auto next = [&](size_t offset) { return static_cast<uint8_t>(value[i + offset]); };
    for (int j = 1; j < length; ++j) {
      if ((next(static_cast<size_t>(j)) & 0xC0) != 0x80) return false;
    }
    if ((c == 0xE0 && next(1) < 0xA0) || (c == 0xED && next(1) >= 0xA0) ||
        (c == 0xF0 && next(1) < 0x90) || (c == 0xF4 && next(1) >= 0x90))
      return false;
    i += static_cast<size_t>(length);
  }
  return true;
}

inline bool validDocument(const Document& doc) {
  if (doc.title.empty() || doc.title.size() > kMaxTitleBytes || doc.body.size() > kMaxBodyBytes) return false;
  if (doc.title.find_first_not_of(" \t") == std::string::npos) return false;
  return isValidUtf8(doc.title, false) && isValidUtf8(doc.body, true);
}

inline bool encode(const Document& doc, std::string& result) {
  if (!validDocument(doc)) return false;
  std::string bytes;
  bytes.reserve(doc.title.size() + doc.body.size() + 5);
  bytes += "# ";
  bytes += doc.title;
  bytes += "\n\n";
  bytes += doc.body;
  bytes += '\n';
  result = std::move(bytes);
  return true;
}

inline bool decode(std::string_view bytes, Document& result) {
  if (bytes.size() < 5 || bytes.size() > kMaxFileBytes || bytes.substr(0, 2) != "# ") return false;
  const size_t endTitle = bytes.find('\n', 2);
  if (endTitle == std::string_view::npos || endTitle + 1 >= bytes.size() || bytes[endTitle + 1] != '\n') return false;
  Document candidate;
  candidate.title = std::string(bytes.substr(2, endTitle - 2));
  auto body = bytes.substr(endTitle + 2);
  if (body.empty() || body.back() != '\n') return false;
  body.remove_suffix(1);
  candidate.body = std::string(body);
  if (!validDocument(candidate)) return false;
  result = std::move(candidate);
  return true;
}

inline bool validFileName(std::string_view name) {
  if (name.size() != kFileNameBytes || name.substr(0, 5) != "note-" || name.substr(21) != ".md") return false;
  for (size_t i = 5; i < 21; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

inline std::string fileName(uint32_t high, uint32_t low) {
  char name[32];
  snprintf(name, sizeof(name), "note-%08lx%08lx.md", static_cast<unsigned long>(high),
           static_cast<unsigned long>(low));
  return name;
}

inline std::string pathFor(std::string_view name) {
  if (!validFileName(name)) return {};
  std::string path(kDirectory);
  path += '/';
  path.append(name);
  return path;
}
}  // namespace nativeapps::notes
