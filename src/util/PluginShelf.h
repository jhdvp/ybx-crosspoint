#pragma once

#include <FsHelpers.h>

#include <string>

#include "network/ProtectedPaths.h"

// Policy for the catalog "On Reader" shelf: which planned file a row may show
// as on-shelf, open, or delete. Kept header-only and free of UI/hardware types
// so the host suite (test/plugin_shelf) drives the same rules the firmware uses.
//
// Removal deletes a path derived from server-supplied data (dest_dir override,
// item title/id), so three gates must all hold before anything is deleted:
//  - the file sits directly in the manifest's own download.dest_dir (a
//    user-editable config.json "dest_dir" may redirect real downloads, but it
//    never moves what the shelf may delete);
//  - the name is one safe component with a reflowable or XTC book extension,
//    so no credential store, settings file, or unrelated data file is
//    reachable even when dest_dir points somewhere unusual;
//  - a marker file (written only by downloadBook after verification) exists,
//    so a user's own same-named file is never treated as firmware-downloaded.
namespace pluginshelf {

// Suffix of the provenance marker next to a firmware-downloaded book.
inline constexpr const char MARKER_SUFFIX[] = ".downloaded";

inline std::string markerPath(const std::string& bookPath) { return bookPath + MARKER_SUFFIX; }

// True when the path names a file the reader can open: reflowable (.epub,
// .txt, .md) or XTC (.xtc/.xtch). Anything else must never be planned, opened,
// or deleted through the shelf.
inline bool isBookPath(const std::string& path) {
  return FsHelpers::hasReflowableBookExtension(path) || FsHelpers::hasXtcExtension(path);
}

// Builds the shelf path for one download filename under the manifest's own
// destination directory. Returns "" when the directory is missing, is the SD
// root, or fails the plugin-path checks; when the filename is not a single
// safe component; or when the result is not a book file.
inline std::string plannedPath(const std::string& manifestDestDir, const std::string& filename) {
  if (manifestDestDir.empty() || filename.empty()) return "";
  std::string dir = manifestDestDir;
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  if (dir.size() <= 1 || !protectedpaths::isPluginPath(dir)) return "";
  if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos || filename.front() == '.') {
    return "";
  }
  const std::string path = dir + "/" + filename;
  if (!protectedpaths::isPluginPath(path) || !isBookPath(path)) return "";
  return path;
}

}  // namespace pluginshelf
