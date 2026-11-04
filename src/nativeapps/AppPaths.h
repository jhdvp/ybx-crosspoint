#pragma once

#include <cstddef>
#include <cstring>

#include "CatalogPolicy.h"

namespace nativeapps {

// Only an app's declared ID determines its storage namespace. These are path
// constructors, not an OS sandbox: compiled native apps still require review
// and can access the HAL. Callers must use Storage's read/write APIs safely.
enum class Directory { Content, State };

inline bool rootPath(const char* id, Directory directory, char* output, size_t capacity) {
  if (!output || capacity == 0) return false;
  output[0] = '\0';
  if (!catalog::validId(id)) return false;
  const char* prefix = directory == Directory::Content ? "/apps/" : "/.crosspoint/apps/";
  const size_t prefixLength = std::strlen(prefix);
  const size_t idLength = std::strlen(id);
  if (prefixLength + idLength + 1 > capacity) return false;
  std::memcpy(output, prefix, prefixLength);
  std::memcpy(output + prefixLength, id, idLength + 1);
  return true;
}

}  // namespace nativeapps
