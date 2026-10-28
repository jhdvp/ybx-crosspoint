#pragma once

#include <map>
#include <string>
#include <vector>

// Native stand-in for the SD-backed HalStorage surface PluginUpdateCache uses,
// backed by an in-memory table. The counters and one-shot failure hooks let a
// test assert how often the cache file is read and what a failing SD write
// leaves behind.

// Arduino String stand-in at global scope: the code under test builds one for
// writeFile, exactly as it does against the real HAL.
class String {
 public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}

  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }

 private:
  std::string value_;
};

namespace fakefs {

inline std::map<std::string, std::string> files;
inline std::vector<std::string> directories;
inline unsigned reads = 0;
inline unsigned writes = 0;
inline unsigned swaps = 0;
// When >= 0, the operation fails once the countdown reaches 0.
inline int failWrite = -1;
inline int failSwap = -1;

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
  reads = 0;
  writes = 0;
  swaps = 0;
  failWrite = -1;
  failSwap = -1;
}

inline void setFile(const std::string& path, const std::string& content) { files[path] = content; }
inline bool hasFile(const std::string& path) { return files.count(path) != 0; }
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

  // Mirrors the real contract: missing, empty, or above-cap reads fail.
  bool readFileToString(const char*, const std::string& path, size_t cap, std::string& out) {
    fakefs::reads++;
    const auto found = fakefs::files.find(path);
    if (found == fakefs::files.end() || found->second.empty() || found->second.size() > cap) return false;
    out = found->second;
    return true;
  }

  bool writeFile(const char* path, const ::String& content) {
    if (fakefs::countdownReached(fakefs::failWrite)) return false;
    fakefs::writes++;
    fakefs::files[path] = content.c_str();
    return true;
  }

  bool ensureDirectoryExists(const char* path) {
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
