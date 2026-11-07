#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "PluginLocations.h"

// Installed plugins whose Plugin Hub catalog version is newer than the copy on
// SD. The catalog is scanned only while the hub is open anyway (its response
// is already streamed to the browse temp file), at most once a day, and the
// result is cached so Home can show the count and one prompt per new version.
// The whole feature is only actionable while Plugin Hub is installed: without
// it no catalog row can be updated, so callers must gate on that.
namespace pluginupdates {

inline constexpr int64_t CHECK_INTERVAL_SECONDS = 24 * 60 * 60;
// Stored entries drive the picker row badges and the Home prompt. The count
// also covers the plugins that did not fit; a duplicate row of a plugin already
// stored is ignored, so the badge counts plugins rather than catalog rows.
inline constexpr size_t MAX_ENTRIES = 12;
inline constexpr size_t MAX_NAME_BYTES = 32;
inline constexpr size_t MAX_TITLE_BYTES = 64;
inline constexpr size_t MAX_VERSION_BYTES = 32;  // three 32-bit components + dots
inline constexpr const char* CACHE_PATH = "/.crosspoint/plugin-updates.json";

struct Entry {
  std::string name;     // plugin folder / catalog id
  std::string title;    // catalog display name
  std::string version;  // catalog version offered
  bool notified = false;
};

struct UpdateSet {
  uint16_t count = 0;  // distinct plugins with an update, including those past MAX_ENTRIES
  std::vector<Entry> entries;
};

// Catalog ids become SD folder names and prompt text; reject anything that
// cannot be one (path separators, dot aliases, controls, oversized names).
inline bool isSafePluginName(const std::string& name) {
  if (name.empty() || name.size() > MAX_NAME_BYTES || name == "." || name == "..") return false;
  for (const char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f || c == '/' || c == '\\') return false;
  }
  return true;
}

// Truncates on a UTF-8 codepoint boundary and drops ASCII controls so a long
// or malformed catalog title cannot corrupt the cache file or the prompt.
inline std::string boundedText(const std::string& text, const size_t maxBytes) {
  std::string out;
  out.reserve(text.size() < maxBytes ? text.size() : maxBytes);
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f) continue;
    out += c;
  }
  if (out.size() > maxBytes) {
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

// Adds a catalog row when its version is newer than the installed copy.
// `installedVersion` is null when the plugin is not installed at all; an
// installed copy without a valid version is offered the update, matching the
// catalog row badge. A row for a plugin already stored is a duplicate of the
// same update, so it is ignored rather than counted twice.
inline bool addCatalogEntry(UpdateSet& set, const std::string& name, const std::string& title,
                            const std::string& catalogVersion, const std::string* installedVersion) {
  if (installedVersion == nullptr || !isSafePluginName(name) || catalogVersion.size() > MAX_VERSION_BYTES) {
    return false;
  }
  for (const auto& entry : set.entries) {
    if (entry.name == name) return false;
  }
  uint32_t parsed[3];
  if (!PluginLocations::parseVersion(catalogVersion, parsed)) return false;
  if (!PluginLocations::isNewerVersion(catalogVersion, *installedVersion)) return false;
  set.count++;
  if (set.entries.size() >= MAX_ENTRIES) return true;
  Entry entry;
  entry.name = name;
  entry.title = boundedText(title.empty() ? name : title, MAX_TITLE_BYTES);
  entry.version = catalogVersion;
  set.entries.push_back(std::move(entry));
  return true;
}

// Keeps the once-per-version promise across refreshes: only an unchanged
// name and catalog version carries its notified flag into the fresh set.
inline void carryNotified(UpdateSet& fresh, const UpdateSet& previous) {
  for (auto& entry : fresh.entries) {
    for (const auto& old : previous.entries) {
      if (old.name == entry.name && old.version == entry.version) {
        entry.notified = old.notified;
        break;
      }
    }
  }
}

// First stored entry the user has not been prompted about, or -1.
inline int pendingIndex(const UpdateSet& set) {
  for (size_t i = 0; i < set.entries.size(); i++) {
    if (!set.entries[i].notified) return static_cast<int>(i);
  }
  return -1;
}

inline void markNotified(UpdateSet& set, const int index) {
  if (index < 0 || index >= static_cast<int>(set.entries.size())) return;
  set.entries[index].notified = true;
}

inline bool removeEntry(UpdateSet& set, const std::string& name) {
  for (size_t i = 0; i < set.entries.size(); i++) {
    if (set.entries[i].name != name) continue;
    set.entries.erase(set.entries.begin() + static_cast<std::ptrdiff_t>(i));
    if (set.count > 0) set.count--;
    return true;
  }
  return false;
}

// Daily throttle. Without a trustworthy clock only the caller's first scan
// runs; once the clock syncs, a cache stamped 0 is refreshed immediately.
inline bool shouldCheck(const int64_t checkedAt, const int64_t now) {
  if (now <= 0) return false;
  if (checkedAt <= 0 || now < checkedAt) return true;
  return now - checkedAt >= CHECK_INTERVAL_SECONDS;
}

// SD persistence (PluginUpdateCache.cpp). load() clears `set` on failure.
bool load(UpdateSet& set, int64_t& checkedAt);
// Writes the file and adopts `set` into cached(): pass a set this call owns,
// not the copy cached() returns.
bool save(const UpdateSet& set, int64_t checkedAt);

struct Snapshot {
  UpdateSet set;
  int64_t checkedAt = 0;
  bool loaded = false;  // false: no readable cache file
};

// The cache file held in RAM: Home reads it on every entry and the hub parses
// a catalog page per browse, so re-reading and re-parsing the whole file there
// costs SD time on the UI task for bytes only this module writes. save() and
// the pruning paths below keep this copy in step; it holds at most MAX_ENTRIES
// bounded strings and lives until invalidate() or a reboot.
const Snapshot& cached();
// Forgets the RAM copy after CACHE_PATH is removed outside save().
void invalidate();
inline uint16_t cachedCount() { return cached().set.count; }
inline bool hasCachedUpdate(const std::string& name) {
  for (const auto& entry : cached().set.entries) {
    if (entry.name == name) return true;
  }
  return false;
}

// Version from `dir`/manifest.json; false when the folder carries no readable
// manifest. A parsed manifest without a version yields an empty `version`.
bool readManifestVersion(const std::string& dir, std::string& version);
// Installed plugin name and manifest version, collected for one catalog scan.
using InstalledMap = std::vector<std::pair<std::string, std::string>>;
// The recorded version, or null when the catalog row is not installed.
inline const std::string* installedVersionOf(const InstalledMap& installed, const std::string& name) {
  for (const auto& record : installed) {
    if (record.first == name) return &record.second;
  }
  return nullptr;
}
// Version from the installed plugin's manifest.json; false when the plugin is
// absent or the manifest carries no readable version.
bool installedVersion(const std::string& name, std::string& version);
// Takes the next promptable entry from cached(): drops entries whose plugin was
// removed or already updated, marks the returned one notified, and saves.
// False when nothing is left to prompt.
bool takePrompt(Entry& out);
// Drops `name` from cached() after an in-app install, update, or uninstall.
bool forget(const std::string& name);

}  // namespace pluginupdates
