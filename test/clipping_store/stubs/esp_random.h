#pragma once
#include <cstddef>
#include <cstdint>

// Deterministic host PRNG with a >2^32-step stream, unlike the previous
// 8-bit counter which repeated an ID after exactly 16 clippings.
// Test-only forced collision verifies the production retry/fail-closed path.
namespace fake_random {
inline bool forceCollision = false;
inline uint64_t state = UINT64_C(0x6a09e667f3bcc909);
}
inline void esp_fill_random(void* buffer, size_t size) {
  auto* bytes = static_cast<uint8_t*>(buffer);
  for (size_t i = 0; i < size; ++i) {
    if (fake_random::forceCollision) {
      bytes[i] = 0xA5;
      continue;
    }
    fake_random::state ^= fake_random::state >> 12;
    fake_random::state ^= fake_random::state << 25;
    fake_random::state ^= fake_random::state >> 27;
    const uint64_t mixed = fake_random::state * UINT64_C(2685821657736338717);
    bytes[i] = static_cast<uint8_t>(mixed >> 56);
  }
}
