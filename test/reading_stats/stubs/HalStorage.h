#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// In-memory stand-in for the SD-backed storage, so the record-file merge can be
// tested without a card. Only the calls ReadingStats.cpp makes are modelled.
namespace fake {

inline std::map<std::string, std::vector<uint8_t>> files;
inline int failRename = -1;  // >= 0: fail the Nth rename that follows

inline void reset() {
  files.clear();
  failRename = -1;
}

inline bool failOne(int& countdown) {
  if (countdown < 0) return false;
  if (countdown == 0) {
    countdown = -1;
    return true;
  }
  --countdown;
  return false;
}

inline void put(const std::string& path, const void* data, const size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  files[path] = std::vector<uint8_t>(bytes, bytes + length);
}

inline std::string content(const std::string& path) {
  const auto it = files.find(path);
  if (it == files.end()) return {};
  return std::string(reinterpret_cast<const char*>(it->second.data()), it->second.size());
}

}  // namespace fake

class HalFile {
 public:
  HalFile() = default;

  bool isOpen() const { return open_; }
  explicit operator bool() const { return open_; }

  size_t read(void* buffer, const size_t count) {
    const auto it = fake::files.find(path_);
    if (!open_ || it == fake::files.end() || position_ >= it->second.size()) return 0;
    const size_t taken = std::min(count, it->second.size() - position_);
    std::memcpy(buffer, it->second.data() + position_, taken);
    position_ += taken;
    return taken;
  }

  size_t write(const void* buffer, const size_t count) {
    if (!open_) return 0;
    auto& bytes = fake::files[path_];
    if (position_ + count > bytes.size()) bytes.resize(position_ + count);
    std::memcpy(bytes.data() + position_, buffer, count);
    position_ += count;
    return count;
  }
  size_t write(const uint8_t* buffer, const size_t count) { return write(static_cast<const void*>(buffer), count); }

  bool seek(const size_t position) {
    if (!open_) return false;
    position_ = position;
    return true;
  }

  size_t fileSize() const {
    const auto it = fake::files.find(path_);
    return it == fake::files.end() ? 0 : it->second.size();
  }

  bool close() {
    open_ = false;
    return true;
  }

 private:
  friend class HalStorage;
  std::string path_;
  size_t position_ = 0;
  bool open_ = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool ensureDirectoryExists(const char*) { return true; }
  bool exists(const char* path) const { return fake::files.count(path) != 0; }
  bool remove(const char* path) { return fake::files.erase(path) != 0; }

  bool rename(const char* from, const char* to) {
    if (fake::failOne(fake::failRename)) return false;
    const auto it = fake::files.find(from);
    if (it == fake::files.end() || fake::files.count(to) != 0) return false;
    fake::files[to] = it->second;
    fake::files.erase(it);
    return true;
  }

  // Mirrors the SDK: remove then rename, so a failed rename can leave the live
  // path absent while the temp copy survives.
  bool replaceFile(const char* tmpPath, const char* path) {
    return (!exists(path) || remove(path)) && rename(tmpPath, path);
  }

  bool readFileToString(const char*, const std::string& path, const size_t cap, std::string& out) {
    const auto it = fake::files.find(path);
    if (it == fake::files.end() || it->second.empty() || it->second.size() > cap) return false;
    out.assign(reinterpret_cast<const char*>(it->second.data()), it->second.size());
    return true;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return open(file, path, /*writing=*/false); }
  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }

  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (!open(file, path, /*writing=*/true)) return false;
    fake::files[file.path_] = {};
    return true;
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    return openFileForWrite(module, path.c_str(), file);
  }

 private:
  bool open(HalFile& file, const char* path, const bool writing) {
    if (!writing && fake::files.count(path) == 0) return false;
    file = HalFile();
    file.path_ = path;
    file.open_ = true;
    return true;
  }
};

#define Storage HalStorage::getInstance()
