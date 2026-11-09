"""Exercise the production settings persistence methods with SD and NVS fakes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/CrossPointSettings.cpp").read_text()
methods = source[source.index("void CrossPointSettings::loadPowerSettings()"):
                 source.index("void CrossPointSettings::validateFrontButtonMapping")]
fixture = r'''
#include <HalPowerSettings.h>
#include <Preferences.h>
#include <Logging.h>
#include <cassert>
#include <mutex>
struct JsonVariantConst {};
struct JsonDocument { template<class T> T as() { return {}; } };
struct CrossPointSettings {
  enum { IGNORE=0, SLEEP=1, SHORT_PWRBTN_COUNT=10, DARK=0, SLEEP_SCREEN_MODE_COUNT=8,
         QUICK_RESUME_NEVER=0, QUICK_RESUME_SLEEP_SCREEN_COUNT=2, FIT=0, NO_FILTER=0,
         SLEEP_SCREEN_COVER_MODE_COUNT=2, SLEEP_SCREEN_COVER_FILTER_COUNT=3,
         MIN_SLEEP_TIMEOUT_MINUTES=1, MAX_SLEEP_TIMEOUT_MINUTES=31 };
  uint8_t sleepTimeoutMinutes=10, shortPwrBtn=0, sleepScreen=0, quickResumeSleepScreen=0;
  uint8_t sleepScreenCoverMode=0, sleepScreenCoverFilter=0, screenInverted=0;
  mutable std::mutex storeMutex;
  bool resaveRequested=false;
  bool sdPresent=false, legacy=false, hasTimeout=true;
  uint8_t sdTimeout=20;
  mutable unsigned sdWrites=0;
  void loadPowerSettings();
  bool saveToFile() const;
  bool loadFromFile();
  static const char* getFilePath() { return "settings"; }
  bool readDocFromFile(const char*, JsonDocument&) { return sdPresent; }
  bool fromJson(JsonVariantConst) {
    if (hasTimeout) sleepTimeoutMinutes=sdTimeout;
    resaveRequested=legacy;
    return true;
  }
  void toJson(JsonDocument&) const {}
  bool writeDocToFile(const char*, const JsonDocument&) const { ++sdWrites; return sdPresent; }
};
'''
checks = r'''
int main() {
  CrossPointSettings saved;
  saved.sdPresent=true;
  saved.sleepTimeoutMinutes=31;
  saved.shortPwrBtn=saved.SLEEP;
  assert(saved.saveToFile());
  assert(writes==1);
  CrossPointSettings noSd;
  noSd.loadPowerSettings();
  assert(!noSd.loadFromFile());
  assert(noSd.sleepTimeoutMinutes==31 && noSd.shortPwrBtn==noSd.SLEEP);
  noSd.sdPresent=true;
  assert(noSd.loadFromFile());
  assert(noSd.sleepTimeoutMinutes==20 && noSd.shortPwrBtn==noSd.IGNORE);
  assert(writes==2);
  CrossPointSettings nextBoot;
  nextBoot.loadPowerSettings();
  assert(nextBoot.sleepTimeoutMinutes==20 && nextBoot.shortPwrBtn==nextBoot.IGNORE);
  assert(noSd.saveToFile());
  assert(noSd.loadFromFile());
  assert(writes==2);
  noSd.legacy=true;
  assert(noSd.loadFromFile()); // migration resave must not deadlock
  assert(writes==2);
  noSd.hasTimeout=false;
  assert(noSd.loadFromFile());
  assert(noSd.sleepTimeoutMinutes==10); // older SD uses defaults, not flash
  flash[1]^=0x80;
  CrossPointSettings corrupt;
  corrupt.loadPowerSettings();
  assert(corrupt.sleepTimeoutMinutes==10 && corrupt.shortPwrBtn==corrupt.IGNORE);
}
'''
with tempfile.TemporaryDirectory(prefix="cp-power-") as temp:
    harness = Path(temp) / "check.cpp"
    harness.write_text(fixture + methods + checks)
    binary = Path(temp) / "check"
    subprocess.run(["c++", "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "test/power_settings/stubs"),
                    "-I" + str(ROOT / "lib/hal"), str(harness),
                    str(ROOT / "lib/hal/HalPowerSettings.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)

# Boot must load flash before the mount can return early; recovery preserves zero.
main = (ROOT / "src/main.cpp").read_text()
setup = main[main.index("void setup() {"):]
assert setup.index("SETTINGS.loadPowerSettings();") < setup.index("if (!Storage.begin())")
recovery = main[main.index("void loop() {"):main.index("static unsigned long maxLoopDuration")]
assert "const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();" in recovery
assert "nosd::kDefaultSleepTimeoutMs" not in recovery
print("Power settings persistence integration: PASS")
