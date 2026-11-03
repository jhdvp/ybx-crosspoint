#pragma once

#include <cstddef>

// Pure C++ registry rules; no Arduino, SD, PluginHub, global constructors that
// own resources, or network services. Also compiled as host regression tests.
namespace nativeapps::catalog {
inline constexpr size_t kMaxApps = 12;
inline constexpr size_t kMaxIdLength = 24;

constexpr bool validId(const char* id) {
  if (!id || id[0] < 'a' || id[0] > 'z') return false;
  size_t length = 0;
  while (id[length] != 0) {
    const char ch = id[length];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    if (++length > kMaxIdLength) return false;
  }
  return id[length - 1] != '-';
}

constexpr bool sameId(const char* a, const char* b) {
  if (!a || !b) return false;
  size_t i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return false;
    ++i;
  }
  return a[i] == b[i];
}

template <typename Entry>
constexpr bool valid(const Entry* entries, size_t count) {
  if (count > kMaxApps || (count != 0 && !entries)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!validId(entries[i].id) || !entries[i].create) return false;
    for (size_t j = 0; j < i; ++j) {
      if (sameId(entries[i].id, entries[j].id)) return false;
    }
  }
  return true;
}

template <typename Entry>
size_t availableCount(const Entry* entries, size_t count) {
  if (!valid(entries, count)) return 0;
  size_t result = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!entries[i].supported || entries[i].supported()) ++result;
  }
  return result;
}

template <typename Entry>
const Entry* availableAt(const Entry* entries, size_t count, size_t availableIndex) {
  if (!valid(entries, count)) return nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (entries[i].supported && !entries[i].supported()) continue;
    if (availableIndex-- == 0) return entries + i;
  }
  return nullptr;
}
}  // namespace nativeapps::catalog
