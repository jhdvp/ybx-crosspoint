#pragma once

#include <cstdint>

namespace pluginevents {

// Durations are below 2^31 ms. Unsigned subtraction handles millis() rollover.
class EventDeadline {
  uint32_t started;
  uint32_t duration;

 public:
  EventDeadline(uint32_t now, uint32_t limit) : started(now), duration(limit) {}
  uint32_t remaining(uint32_t now) const {
    if (duration == 0) return UINT32_MAX;
    const uint32_t elapsed = now - started;
    return elapsed < duration ? duration - elapsed : 0;
  }
};

}  // namespace pluginevents
