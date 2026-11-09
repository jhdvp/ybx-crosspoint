#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
inline std::vector<uint8_t> flash;
inline unsigned writes = 0;
inline bool available = true;
inline bool failWrite = false;
struct Preferences {
  bool begin(const char*, bool) { return available; }
  size_t getBytesLength(const char*) { return flash.size(); }
  size_t getBytes(const char*, void* out, size_t size) {
    if (size != flash.size()) return 0;
    memcpy(out, flash.data(), size);
    return size;
  }
  size_t putBytes(const char*, const void* in, size_t size) {
    ++writes;
    if (failWrite) return 0;
    const auto* data = static_cast<const uint8_t*>(in);
    flash.assign(data, data + size);
    return size;
  }
  void end() {}
};
