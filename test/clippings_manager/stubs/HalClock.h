#pragma once
#include <ctime>

namespace fake_clock {
// Deterministic wall clock for ClippingsManager "Added on ..." formatting.
// mktime is not needed by the tested code path.
inline struct tm local{};
inline bool available = true;
inline bool requested = false;
}  // namespace fake_clock

class HalClock {
 public:
  bool localTime(struct tm& out) const {
    fake_clock::requested = true;
    if (!fake_clock::available) return false;
    out = fake_clock::local;
    return true;
  }
};

inline HalClock halClock;
