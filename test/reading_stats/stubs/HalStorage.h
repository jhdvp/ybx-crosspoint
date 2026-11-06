#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace fake_sd {
struct Disk {
  std::map<std::string, std::string> files;
  std::string failWritePath;  // writes to this path fail
  bool available = true;
  int operations = 0;
  int failAt = -1;
  bool tearWrite = false;
  void reset() { *this = Disk{}; }
};
inline Disk disk;
}  // namespace fake_sd

struct HalFile {
  std::string path;
  size_t pos = 0;
  bool opened = false;
  bool writing = false;
  size_t write(const void* src, size_t count) {
    if (!opened || !writing) return 0;
    if (!fake_sd::disk.failWritePath.empty() && path == fake_sd::disk.failWritePath) return 0;
    if (++fake_sd::disk.operations == fake_sd::disk.failAt) {
      if (!fake_sd::disk.tearWrite) return 0;
      count /= 2;
    }
    fake_sd::disk.files[path].append(static_cast<const char*>(src), count);
    return count;
  }
  int read(void* dest, size_t count) {
    if (!opened || writing) return 0;
    const auto it = fake_sd::disk.files.find(path);
    if (it == fake_sd::disk.files.end()) return -1;
    const auto& data = it->second;
    if (pos >= data.size()) return 0;
    count = std::min(count, data.size() - pos);
    std::copy_n(data.data() + pos, count, static_cast<char*>(dest));
    pos += count;
    return static_cast<int>(count);
  }
  size_t fileSize() const {
    const auto it = fake_sd::disk.files.find(path);
    return it == fake_sd::disk.files.end() ? 0 : it->second.size();
  }
  void flush() {}
  bool close() {
    opened = false;
    return fake_sd::disk.available;
  }
};

struct HalStorage {
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  bool ensureDirectoryExists(const char*) { return fake_sd::disk.available; }
  bool exists(const char* path) { return fake_sd::disk.available && fake_sd::disk.files.count(path) != 0; }
  bool remove(const char* path) {
    if (!fake_sd::disk.available || ++fake_sd::disk.operations == fake_sd::disk.failAt) return false;
    return fake_sd::disk.files.erase(path) == 1;
  }
  bool rename(const char* from, const char* to) {
    if (!fake_sd::disk.available || ++fake_sd::disk.operations == fake_sd::disk.failAt || exists(to)) return false;
    auto it = fake_sd::disk.files.find(from);
    if (it == fake_sd::disk.files.end()) return false;
    fake_sd::disk.files[to] = std::move(it->second);
    fake_sd::disk.files.erase(it);
    return true;
  }
  bool readFileToString(const char*, const std::string& path, size_t cap, std::string& out) {
    out.clear();
    if (!exists(path.c_str())) return false;
    const auto& value = fake_sd::disk.files.at(path);
    if (value.empty() || value.size() > cap) return false;
    out = value;
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& f) {
    if (!fake_sd::disk.available) return false;
    fake_sd::disk.files[path].clear();
    f.path = path;
    f.pos = 0;
    f.opened = true;
    f.writing = true;
    return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& f) {
    if (!fake_sd::disk.available || fake_sd::disk.files.count(path) == 0) return false;
    f.path = path;
    f.pos = 0;
    f.opened = true;
    f.writing = false;
    return true;
  }
};
#define Storage HalStorage::getInstance()
