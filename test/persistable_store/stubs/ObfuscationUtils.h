#pragma once

// Host-test stand-in: PersistableStore.cpp's extractPassword() links against
// this surface, but these tests exercise the file swap, not credential
// decoding, so the stub always reports "not obfuscated" and never allocates.

#include <cstddef>
#include <string>

namespace obfuscation {

inline std::string deobfuscateFromBase64(const char*, size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = false;
  if (tooLong) *tooLong = false;
  return std::string();
}

inline std::string deobfuscateFromBase64(const char*, bool* ok = nullptr) {
  if (ok) *ok = false;
  return std::string();
}

}  // namespace obfuscation
