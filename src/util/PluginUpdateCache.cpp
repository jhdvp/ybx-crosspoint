#include "PluginUpdateCache.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace pluginupdates {
namespace {
// The generated file is ~150 bytes per entry; the cap bounds a corrupt card.
constexpr size_t MAX_CACHE_FILE_BYTES = 4096;
constexpr char TMP_PATH[] = "/.crosspoint/plugin-updates.json.tmp";
// Installed manifest.json only needs its version field; the file itself is
// small, but cap it like the catalog's own manifest read.
constexpr size_t MAX_INSTALLED_MANIFEST_BYTES = 8 * 1024;

// cached()'s storage. Only this module writes CACHE_PATH, so save() and the
// pruning paths below keep it in step; invalidate() drops it when the hub
// uninstall removes the file.
Snapshot memo;
// Set once the file has been read; cleared by invalidate().
bool memoLoaded = false;

// Writes CACHE_PATH through the temp file, leaving the memo untouched.
bool writeCache(const UpdateSet& set, const int64_t checkedAt) {
  JsonDocument doc;
  doc["checked_at"] = checkedAt > 0 ? checkedAt : 0;
  doc["count"] = set.count;
  JsonArray items = doc["items"].to<JsonArray>();
  for (const auto& entry : set.entries) {
    JsonObject item = items.add<JsonObject>();
    item["name"] = entry.name;
    item["title"] = entry.title;
    item["version"] = entry.version;
    item["notified"] = entry.notified;
  }
  std::string raw;
  serializeJson(doc, raw);
  if (raw.size() > MAX_CACHE_FILE_BYTES) {
    LOG_ERR("PUPD", "update cache too large: %u bytes", static_cast<unsigned>(raw.size()));
    return false;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  if (!Storage.writeFile(TMP_PATH, String(raw.c_str()))) {
    LOG_ERR("PUPD", "update cache write failed");
    return false;
  }
  if (!Storage.replaceFile(TMP_PATH, CACHE_PATH)) {
    Storage.remove(TMP_PATH);
    LOG_ERR("PUPD", "update cache swap failed");
    return false;
  }
  return true;
}
}  // namespace

bool readManifestVersion(const std::string& dir, std::string& version) {
  std::string raw;
  if (!Storage.readFileToString("PUPD", dir + "/manifest.json", MAX_INSTALLED_MANIFEST_BYTES, raw)) return false;
  JsonDocument filter;
  filter["version"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
  version = doc["version"] | "";
  return true;
}

bool installedVersion(const std::string& name, std::string& version) {
  const std::string dir = PluginLocations::findPluginDir(name.c_str());
  if (dir.empty()) return false;
  return readManifestVersion(dir, version);
}

const Snapshot& cached() {
  if (!memoLoaded) {
    memoLoaded = true;
    memo.loaded = load(memo.set, memo.checkedAt);
  }
  return memo;
}

void invalidate() {
  memo = Snapshot{};
  memoLoaded = false;
}

bool load(UpdateSet& set, int64_t& checkedAt) {
  set = UpdateSet{};
  checkedAt = 0;
  std::string raw;
  if (!Storage.readFileToString("PUPD", CACHE_PATH, MAX_CACHE_FILE_BYTES, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  if (items.isNull()) return false;
  checkedAt = doc["checked_at"] | static_cast<int64_t>(0);
  for (JsonVariantConst item : items) {
    if (set.entries.size() >= MAX_ENTRIES) break;
    const std::string name = item["name"] | "";
    const std::string version = item["version"] | "";
    if (!isSafePluginName(name) || version.empty() || version.size() > MAX_VERSION_BYTES) continue;
    Entry entry;
    entry.name = name;
    entry.title = boundedText(item["title"] | "", MAX_TITLE_BYTES);
    entry.version = version;
    entry.notified = item["notified"] | false;
    set.entries.push_back(std::move(entry));
  }
  const uint32_t storedCount = doc["count"] | 0;
  set.count = static_cast<uint16_t>(std::min<uint32_t>(storedCount, UINT16_MAX));
  set.count = std::max<uint16_t>(set.count, static_cast<uint16_t>(set.entries.size()));
  return true;
}

bool save(const UpdateSet& set, const int64_t checkedAt) {
  if (!writeCache(set, checkedAt)) return false;
  memo.set = set;
  memo.checkedAt = checkedAt;
  memo.loaded = true;
  memoLoaded = true;
  return true;
}

bool takePrompt(Entry& out) {
  cached();
  UpdateSet& set = memo.set;
  bool changed = false;
  for (size_t i = 0; i < set.entries.size();) {
    if (set.entries[i].notified) {
      i++;
      continue;
    }
    const std::string name = set.entries[i].name;
    std::string installed;
    if (!installedVersion(name, installed) || !PluginLocations::isNewerVersion(set.entries[i].version, installed)) {
      removeEntry(set, name);
      changed = true;
      continue;
    }
    out = set.entries[i];
    set.entries[i].notified = true;
    // The memo already holds this set, so the file write must not copy it back
    // through save(). A failed write leaves the prompt suppressed in RAM: the
    // next boot re-reads the file and offers it again.
    if (!writeCache(set, memo.checkedAt)) LOG_ERR("PUPD", "update prompt not persisted");
    return true;
  }
  if (changed) writeCache(set, memo.checkedAt);
  return false;
}

bool forget(const std::string& name) {
  cached();
  if (!removeEntry(memo.set, name)) return false;
  writeCache(memo.set, memo.checkedAt);
  return true;
}

}  // namespace pluginupdates
