#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "network/TlsPolicy.h"

// Small, allocation-free menu projection: dynamic indexes contain folder
// descriptors mixed with leaf items. Only children of the active group render.
// These helpers are deliberately host-testable without Arduino/FreeInk.
namespace catalogfolders {

template <typename Entry>
int visibleCount(const std::vector<Entry>& entries, const std::string& parent) {
  int count = 0;
  for (const auto& entry : entries) {
    if (entry.parent == parent) ++count;
  }
  return count;
}

template <typename Entry>
int indexAt(const std::vector<Entry>& entries, const std::string& parent, const int selected) {
  if (selected < 0) return -1;
  int ordinal = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].parent == parent && ordinal++ == selected) return static_cast<int>(i);
  }
  return -1;
}

inline bool canEnter(const std::vector<std::string>& trail, const std::string& id) {
  return !id.empty() && trail.size() < 8 && std::find(trail.begin(), trail.end(), id) == trail.end();
}

// The normal browse Authorization header is reused for folder requests. Only
// allow that request inside the same verified HTTPS origin, not just a shared
// hostname suffix. This is also exercised by host tests.
inline bool canFollowAuthenticatedFolder(const std::string& currentUrl, const std::string& folderUrl) {
  const std::string from = tlspolicy::originOf(currentUrl);
  return !from.empty() && from == tlspolicy::originOf(folderUrl) && tlspolicy::isHttpsUrl(folderUrl);
}

}  // namespace catalogfolders
