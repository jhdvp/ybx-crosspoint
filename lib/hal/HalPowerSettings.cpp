#include "HalPowerSettings.h"

#include <Logging.h>
#include <Preferences.h>

#include <cstring>

namespace HalPowerSettings {
namespace {
constexpr char NAMESPACE[] = "cp-power";
constexpr char KEY[] = "settings";
}

bool read(uint8_t (&record)[10]) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, true)) return false;
  const bool ok = prefs.getBytesLength(KEY) == sizeof(record) &&
                  prefs.getBytes(KEY, record, sizeof(record)) == sizeof(record);
  prefs.end();
  if (!ok) return false;
  const uint8_t checksum = static_cast<uint8_t>(~(record[0] ^ record[1] ^ record[2] ^ record[3] ^ record[4] ^
                                                 record[5] ^ record[6] ^ record[7]));
  if (record[0] != 1 || record[8] != 0 || record[9] != checksum) {
    LOG_ERR("POWER", "Invalid flash settings; using defaults");
    return false;
  }
  return true;
}

void write(const uint8_t (&record)[10]) {
  uint8_t previous[10];
  if (read(previous) && memcmp(previous, record, sizeof(record)) == 0) return;
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) {
    LOG_ERR("POWER", "Cannot open flash settings");
    return;
  }
  if (prefs.putBytes(KEY, record, sizeof(record)) != sizeof(record)) {
    LOG_ERR("POWER", "Cannot write flash settings");
  }
  prefs.end();
}
}  // namespace HalPowerSettings
