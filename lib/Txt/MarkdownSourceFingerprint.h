#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// Change detection for derived Markdown pagination/TOC caches. This is NOT
// a security or content-identity hash: it only catches ordinary SD edits,
// including same-size edits on devices with no reliable filesystem mtime.
namespace MarkdownSourceFingerprint {
inline constexpr const char* kMarkerFile = "/markdown-index-v2.fnv";
class Hasher {
 public:
  void add(const uint8_t* data, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      value_ ^= data[i];
      value_ *= UINT64_C(1099511628211);
    }
  }
  void add(std::string_view chunk) { add(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()); }
  uint64_t value() const { return value_; }

 private:
  uint64_t value_ = UINT64_C(14695981039346656037);
};

inline std::string marker(uint64_t value) {
  char text[32];
  std::snprintf(text, sizeof(text), "md2:%016llx", static_cast<unsigned long long>(value));
  return text;
}
}  // namespace MarkdownSourceFingerprint
