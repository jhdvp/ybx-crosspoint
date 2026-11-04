#include "YbxNativeSession.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

namespace {
bool readNative(const char* path, std::string& result) {
  std::string raw;
  if (!Storage.readFileToString("YBX", path, 4096, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return false;
  if ((doc["schema"] | 0) != 1) return false;
  result = doc["access_token"] | "";
  return !result.empty() && result.size() <= 2048;
}
}  // namespace

bool ybxnative::loadToken(std::string& out) {
  out.clear();
  if (readNative(kAccountFile, out)) return true;
  out.clear();
  // Crash-safe recovery after a power loss between main→bak and tmp→main.
  if (readNative("/.crosspoint/ybx-account.json.bak", out)) return true;
  out.clear();

  // Read existing installations even when the Plugin System is disabled.
  const std::string dir = PluginLocations::findPluginDirOnDisk("ybx");
  if (!dir.empty() &&
      pluginhttp::loadTokenFromFile(dir + "/token.json", "access_token", out) &&
      !out.empty()) return true;
  out.clear();
  if (pluginhttp::loadTokenFromFile("/.crosspoint/plugin-data/ybx/token.json", "access_token", out) &&
      !out.empty()) return true;
  out.clear();
  return false;
}

bool ybxnative::saveToken(const std::string& token) {
  if (token.empty() || token.size() > 2048) return false;
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) return false;
  if (!Storage.exists(kCacheRoot) && !Storage.mkdir(kCacheRoot)) return false;
  JsonDocument doc;
  doc["schema"] = 1;
  doc["access_token"] = token;
  std::string serialized;
  serializeJson(doc, serialized);
  constexpr const char* tmp = "/.crosspoint/ybx-account.json.tmp";
  constexpr const char* bak = "/.crosspoint/ybx-account.json.bak";
  Storage.remove(tmp);
  {
    HalFile file;
    if (!Storage.openFileForWrite("YBX", tmp, file)) return false;
    const bool complete = file.write(reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size()) ==
                          serialized.size();
    const bool closed = file.close();
    if (!complete || !closed) {
      Storage.remove(tmp);
      return false;
    }
  }
  Storage.remove(bak);
  const bool hadOld = Storage.exists(kAccountFile);
  if (hadOld && !Storage.rename(kAccountFile, bak)) {
    Storage.remove(tmp);
    return false;
  }
  if (!Storage.rename(tmp, kAccountFile)) {
    if (hadOld) Storage.rename(bak, kAccountFile);
    Storage.remove(tmp);
    return false;
  }
  Storage.remove(bak);
  // Existing catalog plugin is only a compatibility consumer; the native
  // reader must continue to work even if this optional mirror fails.
  const std::string dir = PluginLocations::findPluginDirOnDisk("ybx");
  if (!dir.empty() &&
      !pluginhttp::saveTokenToFile(dir + "/token.json", "access_token", token)) {
    LOG_ERR("YBX", "Could not mirror reader key to optional catalog plugin");
  }
  return true;
}
