#pragma once

#include <cstdint>
#include <string_view>

namespace ybxnote {

// A bounded lightweight revision guard, NOT a cryptographic identity.
// Native Notes are at most ~4 KiB. A changed H1/body alters the revision
// and suppresses the old source-offset highlight rather than painting
// unrelated words. Zero is reserved for unreadable/unknown content.
// The version constant prevents reuse after a future Markdown parser change.
inline uint32_t revisionSignature(std::string_view source) {
  uint32_t hash = 2166136261u;
  constexpr std::string_view domain = "ybx-native-note-md-v2:";
  for (const unsigned char c : domain) {
    hash ^= c;
    hash *= 16777619u;
  }
  for (const unsigned char c : source) {
    hash ^= c;
    hash *= 16777619u;
  }
  return hash ? hash : 1u;
}

}  // namespace ybxnote
