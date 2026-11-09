#pragma once

#include <Logging.h>

#include <cstdint>

// One versioned NVS record; callers serialize access with the settings mutex.
namespace HalPowerSettings {
bool read(uint8_t (&record)[10]);
void write(const uint8_t (&record)[10]);

template <typename Settings>
void reset(Settings& s) {
  s.sleepTimeoutMinutes = 10;
  s.shortPwrBtn = Settings::IGNORE;
  s.sleepScreen = Settings::DARK;
  s.quickResumeSleepScreen = Settings::QUICK_RESUME_NEVER;
  s.sleepScreenCoverMode = Settings::FIT;
  s.sleepScreenCoverFilter = Settings::NO_FILTER;
  s.screenInverted = 0;
}

template <typename Settings>
void load(Settings& s) {
  reset(s);
  uint8_t record[10];
  if (!read(record)) return;
  if (record[1] < Settings::MIN_SLEEP_TIMEOUT_MINUTES || record[1] > Settings::MAX_SLEEP_TIMEOUT_MINUTES ||
      record[2] >= Settings::SHORT_PWRBTN_COUNT || record[3] >= Settings::SLEEP_SCREEN_MODE_COUNT ||
      record[4] >= Settings::QUICK_RESUME_SLEEP_SCREEN_COUNT || record[5] >= Settings::SLEEP_SCREEN_COVER_MODE_COUNT ||
      record[6] >= Settings::SLEEP_SCREEN_COVER_FILTER_COUNT || record[7] > 1) {
    LOG_ERR("POWER", "Flash settings out of range; using defaults");
    return;
  }
  s.sleepTimeoutMinutes = record[1];
  s.shortPwrBtn = record[2];
  s.sleepScreen = record[3];
  s.quickResumeSleepScreen = record[4];
  s.sleepScreenCoverMode = record[5];
  s.sleepScreenCoverFilter = record[6];
  s.screenInverted = record[7];
}

template <typename Settings>
void save(const Settings& s) {
  uint8_t record[10] = {1, s.sleepTimeoutMinutes, s.shortPwrBtn, s.sleepScreen, s.quickResumeSleepScreen,
                        s.sleepScreenCoverMode, s.sleepScreenCoverFilter, s.screenInverted, 0, 0};
  // Complemented payload catches malformed records independently of NVS CRC.
  record[9] = static_cast<uint8_t>(~(record[0] ^ record[1] ^ record[2] ^ record[3] ^ record[4] ^ record[5] ^
                                    record[6] ^ record[7]));
  write(record);
}
}  // namespace HalPowerSettings
