#include <gtest/gtest.h>
#include <HalPowerSettings.h>
#include <Preferences.h>

struct Settings {
  enum { IGNORE = 0, SLEEP = 1, SHORT_PWRBTN_COUNT = 10, DARK = 0, SLEEP_SCREEN_MODE_COUNT = 8,
         QUICK_RESUME_NEVER = 0, QUICK_RESUME_SLEEP_SCREEN_COUNT = 2,
         FIT = 0, NO_FILTER = 0, SLEEP_SCREEN_COVER_MODE_COUNT = 2, SLEEP_SCREEN_COVER_FILTER_COUNT = 3,
         MIN_SLEEP_TIMEOUT_MINUTES = 1, MAX_SLEEP_TIMEOUT_MINUTES = 31 };
  uint8_t sleepTimeoutMinutes = 10;
  uint8_t shortPwrBtn = IGNORE;
  uint8_t sleepScreen = DARK;
  uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;
  uint8_t sleepScreenCoverMode = FIT;
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  uint8_t screenInverted = 0;
};

class PowerSettingsTest : public testing::Test {
 protected:
  void SetUp() override { flash.clear(); writes = 0; available = true; failWrite = false; }
};

TEST_F(PowerSettingsTest, NoSdColdBootUsesFlashIncludingNeverSleep) {
  Settings saved{31, Settings::SLEEP, 6, 1, 1, 2, 1};
  HalPowerSettings::save(saved);
  Settings boot;
  HalPowerSettings::load(boot);
  EXPECT_EQ(boot.sleepTimeoutMinutes, 31);
  EXPECT_EQ(boot.shortPwrBtn, Settings::SLEEP);
  EXPECT_EQ(boot.sleepScreen, 6);
  EXPECT_EQ(boot.quickResumeSleepScreen, 1);
  EXPECT_EQ(boot.sleepScreenCoverMode, 1);
  EXPECT_EQ(boot.sleepScreenCoverFilter, 2);
  EXPECT_EQ(boot.screenInverted, 1);
}

TEST_F(PowerSettingsTest, SdValuesWinAndResyncFlash) {
  HalPowerSettings::save(Settings{1, Settings::SLEEP, 6, 1});
  Settings boot;
  HalPowerSettings::load(boot);
  // The SD loader resets mirrored fields before applying its document.
  HalPowerSettings::reset(boot);
  boot.sleepTimeoutMinutes = 20;
  HalPowerSettings::save(boot);
  Settings nextBoot;
  HalPowerSettings::load(nextBoot);
  EXPECT_EQ(nextBoot.sleepTimeoutMinutes, 20);
  EXPECT_EQ(nextBoot.shortPwrBtn, Settings::IGNORE);
  EXPECT_EQ(nextBoot.sleepScreen, Settings::DARK);
  EXPECT_EQ(writes, 2u);
}

TEST_F(PowerSettingsTest, MissingCorruptAndUnknownVersionUseDefaults) {
  Settings boot{1, 1, 6, 1};
  HalPowerSettings::load(boot);
  EXPECT_EQ(boot.sleepTimeoutMinutes, 10);
  HalPowerSettings::save(Settings{3, 1, 6, 1});
  const auto valid = flash;
  for (size_t i = 0; i < valid.size(); ++i) {
    flash = valid;
    flash[i] ^= 0x80;
    HalPowerSettings::load(boot);
    EXPECT_EQ(boot.sleepTimeoutMinutes, 10) << i;
    EXPECT_EQ(boot.shortPwrBtn, Settings::IGNORE) << i;
  }
  flash.resize(3);
  HalPowerSettings::load(boot);
  EXPECT_EQ(boot.sleepTimeoutMinutes, 10);
}

TEST_F(PowerSettingsTest, OutOfRangeValuesUseDefaults) {
  for (Settings invalid : {Settings{0, 1, 6, 1}, Settings{32, 1, 6, 1}, Settings{10, 10, 6, 1},
                           Settings{10, 1, 8, 1}, Settings{10, 1, 6, 2}}) {
    HalPowerSettings::save(invalid);
    Settings boot;
    HalPowerSettings::load(boot);
    EXPECT_EQ(boot.sleepTimeoutMinutes, 10);
    EXPECT_EQ(boot.shortPwrBtn, Settings::IGNORE);
  }
}

TEST_F(PowerSettingsTest, UnchangedValuesNeverWriteAgain) {
  Settings settings{15, 1, 6, 1};
  HalPowerSettings::save(settings);
  for (int i = 0; i < 10; ++i) {
    HalPowerSettings::load(settings);
    HalPowerSettings::save(settings);
  }
  EXPECT_EQ(writes, 1u);
  settings.shortPwrBtn = Settings::IGNORE;
  HalPowerSettings::save(settings);
  EXPECT_EQ(writes, 2u);
}

TEST_F(PowerSettingsTest, UnavailableNvsAndFailedWriteAreSafeAndRetryable) {
  available = false;
  Settings boot{1, 1, 6, 1};
  HalPowerSettings::load(boot);
  EXPECT_EQ(boot.sleepTimeoutMinutes, 10);
  HalPowerSettings::save(boot);
  EXPECT_EQ(writes, 0u);
  available = true;
  failWrite = true;
  HalPowerSettings::save(boot);
  failWrite = false;
  HalPowerSettings::save(boot);
  EXPECT_EQ(writes, 2u);
  EXPECT_EQ(flash.size(), 10u);
}
