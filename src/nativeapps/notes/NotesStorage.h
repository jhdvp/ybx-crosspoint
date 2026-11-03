#pragma once

#include <string>
#include <vector>

#include "NotesDocument.h"

namespace nativeapps::notes {

struct Summary {
  std::string fileName;
  std::string title;
  bool readable = false;
};

struct List {
  std::vector<Summary> rows;
  bool truncated = false;
  bool failed = false;
};

// All I/O through HalStorage; preserves the last completed valid revision
// in a .bak sidecar during updates. A partial .tmp is never promoted at boot.
bool load(std::string_view fileName, Document& out);
bool save(std::string_view fileName, const Document& value);
bool create(const Document& value, std::string& newFileName);
bool erase(std::string_view fileName);
List list();

}  // namespace nativeapps::notes
