#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>


class MockStorage;

class HalFile {
 public:
  HalFile() = default;
  HalFile(MockStorage* storage, std::string path, bool directory);

  explicit operator bool() const { return storage_ != nullptr; }
  bool isDirectory() const { return directory_; }
  size_t getName(char* out, size_t cap) const;
  HalFile openNextFile();
  size_t write(const void* data, size_t size);
  size_t size() const;
  int read(void* data, size_t size);
  void flush() {}
  bool close();

 private:
  MockStorage* storage_ = nullptr;
  std::string path_;
  bool directory_ = false;
  size_t index_ = 0;
  size_t readOffset_ = 0;
};

class MockStorage {
 public:
  static MockStorage& getInstance() {
    static MockStorage instance;
    return instance;
  }
  void reset() {
    files.clear();
    dirs = {"/", "/apps", "/apps/notes"};
    online = true;
    shortWrite = false;
    shortRead = false;
    closeFailed = false;
    blockFinalRename = false;
    blockBackupRename = false;
    blockDelete = false;
  }
  bool ready() const { return online; }
  bool ensureDirectoryExists(const char* name) {
    if (!online) return false;
    dirs.insert(name);
    return true;
  }
  bool exists(const char* path) const { return files.find(path) != files.end(); }
  bool remove(const char* path) {
    if (blockDelete) return false;
    return files.erase(path) > 0;
  }
  bool rename(const char* src, const char* dest) {
    if (blockBackupRename && std::string(dest).find(".bak") != std::string::npos) return false;
    if (blockFinalRename && std::string(src).find(".tmp") != std::string::npos) return false;
    auto it = files.find(src);
    if (it == files.end() || exists(dest)) return false;
    files[dest] = it->second;
    files.erase(it);
    return true;
  }
  bool readFileToString(const char*, const std::string& path, size_t cap, std::string& out) {
    if (!online || shortRead) return false;
    auto it = files.find(path);
    if (it == files.end() || it->second.empty() || it->second.size() > cap) return false;
    out = it->second;
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    if (!online || !exists(path.c_str())) return false;
    out = HalFile(this, path, false);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    if (!online) return false;
    files[path].clear();
    out = HalFile(this, path, false);
    return true;
  }
  HalFile open(const char* path) { return dirs.count(path) ? HalFile(this, path, true) : HalFile(); }

  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  bool online = true;
  bool shortWrite = false;
  bool shortRead = false;
  bool closeFailed = false;
  bool blockFinalRename = false;
  bool blockBackupRename = false;
  bool blockDelete = false;
  friend class HalFile;
};

inline HalFile::HalFile(MockStorage* storage, std::string path, bool directory)
    : storage_(storage), path_(std::move(path)), directory_(directory) {}
inline size_t HalFile::getName(char* out, size_t cap) const {
  if (!storage_ || cap == 0) return 0;
  const auto lastSlash = path_.find_last_of('/');
  const std::string name = path_.substr(lastSlash + 1);
  if (name.size() >= cap) return 0;
  std::memcpy(out, name.data(), name.size());
  out[name.size()] = '\0';
  return name.size();
}
inline HalFile HalFile::openNextFile() {
  if (!storage_ || !directory_) return {};
  std::string prefix = path_;
  if (prefix.back() != '/') prefix += '/';
  size_t index = 0;
  for (auto const& entry : storage_->files) {
    const std::string_view path(entry.first);
    if (path.substr(0, prefix.size()) == prefix && path.substr(prefix.size()).find('/') == std::string_view::npos) {
      if (index++ == index_) {
        ++index_;
        return HalFile(storage_, entry.first, false);
      }
    }
  }
  return {};
}
inline size_t HalFile::write(const void* data, size_t count) {
  if (!storage_ || directory_) return 0;
  const size_t length = storage_->shortWrite ? count / 2 : count;
  storage_->files[path_].append(static_cast<const char*>(data), length);
  return length;
}
inline size_t HalFile::size() const {
  if (!storage_ || directory_) return 0;
  return storage_->files.at(path_).size();
}
inline int HalFile::read(void* data, size_t maxCount) {
  if (!storage_ || directory_ || storage_->shortRead) return -1;
  const std::string& contents = storage_->files.at(path_);
  const size_t count = std::min(maxCount, contents.size() - readOffset_);
  std::memcpy(data, contents.data() + readOffset_, count);
  readOffset_ += count;
  return static_cast<int>(count);
}
inline bool HalFile::close() {
  if (!storage_) return false;
  const bool success = !storage_->closeFailed;
  storage_ = nullptr;
  return success;
}

#define Storage MockStorage::getInstance()
