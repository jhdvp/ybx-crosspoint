#pragma once

#include <map>
#include <string>
#include <vector>

// Native stand-in for the SD-backed HalStorage surface PersistableStore uses,
// backed by an in-memory table. Counters and failure hooks let tests assert the
// atomic swap sequence and what a failing SD operation leaves behind.
// Mirrors the fakefs shape of test/plugin_update_cache/stubs/HalStorage.h.

namespace fakefs {

inline std::map<std::string, std::string> files;
inline std::vector<std::string> directories;
inline unsigned writes = 0;
inline unsigned swaps = 0;
// When >= 0, the operation fails once the countdown reaches 0.
inline int failWrite = -1;
inline int failSwap = -1;
// Models a dead/removed card: every write fails (not just the first).
inline bool failAllWrites = false;

inline bool countdownReached(int& countdown) {
  if (countdown < 0) return false;
  if (countdown == 0) {
    countdown = -1;
    return true;
  }
  --countdown;
  return false;
}

inline void reset() {
  files.clear();
  directories.clear();
  writes = 0;
  swaps = 0;
  failWrite = -1;
  failSwap = -1;
  failAllWrites = false;
}

inline void setFile(const std::string& path, const std::string& content) { files[path] = content; }
inline std::string fileContents(const std::string& path) {
  const auto found = files.find(path);
  return found == files.end() ? std::string() : found->second;
}

}  // namespace fakefs

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  // Matches the real contract: missing or unreadable files return "".
  ::String readFile(const char* path) {
    const auto found = fakefs::files.find(path);
    return ::String(found == fakefs::files.end() ? "" : found->second.c_str());
  }

  bool writeFile(const char* path, const ::String& content) {
    if (failAllWrites) return false;
    if (fakefs::countdownReached(fakefs::failWrite)) return false;
    fakefs::writes++;
    fakefs::files[path] = content.c_str();
    return true;
  }

  bool mkdir(const char* path, const bool pFlag = true) {
    (void)pFlag;
    fakefs::directories.push_back(path);
    return true;
  }

  bool exists(const char* path) { return fakefs::files.count(path) != 0; }

  bool remove(const char* path) { return fakefs::files.erase(path) != 0; }

  // The SD swap: the temp file replaces the destination and is gone afterwards.
  bool replaceFile(const char* tmpPath, const char* path) {
    if (fakefs::countdownReached(fakefs::failSwap)) return false;
    const auto found = fakefs::files.find(tmpPath);
    if (found == fakefs::files.end()) return false;
    fakefs::swaps++;
    fakefs::files[path] = found->second;
    fakefs::files.erase(found);
    return true;
  }
};

#define Storage HalStorage::getInstance()
