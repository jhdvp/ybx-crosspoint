#pragma once
#include <cstdint>

namespace nosd {

// Retry a missing SD card only after explicit input. No timer-driven mount
// attempts: repeated SPI/SDMMC probing would drain the battery and can race
// with the display's shared SPI bus on C3 models.
struct RetryGate {
  static constexpr uint32_t kRetryCooldownMs = 2000;
  bool attempted = false;
  uint32_t lastAttemptMs = 0;

  bool consume(uint32_t nowMs, bool userRequested) {
    if (!userRequested) return false;
    if (attempted && static_cast<uint32_t>(nowMs - lastAttemptMs) < kRetryCooldownMs) return false;
    attempted = true;
    lastAttemptMs = nowMs;
    return true;
  }
};

// Inactivity timeout for SD recovery mode when settings are not yet loaded.
inline constexpr uint32_t kDefaultSleepTimeoutMs = 300000;  // 5 minutes

inline bool shouldAutoSleep(uint32_t nowMs, uint32_t lastActivityMs, uint32_t timeoutMs = kDefaultSleepTimeoutMs) {
  if (timeoutMs == 0) return false;
  return static_cast<uint32_t>(nowMs - lastActivityMs) >= timeoutMs;
}

inline bool shouldPowerSleep(bool powerPressed, uint32_t heldTimeMs, uint32_t thresholdMs = 500) {
  return powerPressed && heldTimeMs > thresholdMs;
}

}  // namespace nosd
