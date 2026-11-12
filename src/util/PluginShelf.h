#pragma once

#include <FsHelpers.h>

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include "network/ProtectedPaths.h"

// Policy for the catalog "On Reader" shelf: which planned file a row may show
// as on-shelf, open, or delete. Kept header-only and free of UI/hardware types
// so the host suite (test/plugin_shelf) drives the same rules the firmware uses.
//
// Provenance lives in a firmware-owned record directory, not next to the book:
// a name-only marker in the download folder is writable by any manifest that
// controls dest_dir or a sidecar/bundle target, so it could vouch for a user's
// own file. A record path matches the ProtectedPaths::SENSITIVE_FILES prefix,
// so none of those plugin-supplied writes can reach it.
//
// Removal deletes a path derived from server-supplied data (dest_dir override,
// item title/id), so all of these must hold before anything is deleted:
//  - the file sits directly in the manifest's own download.dest_dir (a
//    user-editable config.json "dest_dir" may redirect real downloads, but it
//    never moves what the shelf may delete);
//  - the name is one safe component with a reflowable or XTC book extension,
//    so no credential store, settings file, or unrelated data file is
//    reachable even when dest_dir points somewhere unusual;
//  - the firmware wrote a provenance record for that exact path, and the file
//    is a regular file (a directory named "book.epub" is not a book);
//  - a sidecar is only deleted when downloadBook recorded writing it for this
//    record, and only when it is a ".json" file directly in dest_dir.
namespace pluginshelf {

// Suffix of the per-file marker this firmware used to write next to a
// downloaded book. Nothing reads it: a plugin-controlled write target could
// plant one, so it grants no shelf status and is never adopted into a
// provenance record. The namespace stays reserved so no plugin-supplied write
// can land on it (see isWriteTargetAllowed).
inline constexpr const char MARKER_SUFFIX[] = ".downloaded";

// Firmware-owned provenance records, one per downloaded book, named by the MD5
// of the book's full path so two catalogs sharing a filename cannot vouch for
// each other's files. Listed in ProtectedPaths::SENSITIVE_FILES.
inline constexpr const char PROVENANCE_DIR[] = "/.crosspoint/shelf";

inline bool endsWithIgnoreCase(const std::string& path, const char* suffix) {
  const size_t length = strlen(suffix);
  if (path.size() < length) return false;
  const char* tail = path.c_str() + path.size() - length;
  for (size_t i = 0; i < length; i++) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) != std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

// True when a download/sidecar/bundle target would land in the marker namespace.
inline bool hasMarkerSuffix(const std::string& path) { return endsWithIgnoreCase(path, MARKER_SUFFIX); }

// Record path for the digest of a book path (the caller hashes it; the shelf
// only ever needs "did we download exactly this file"). "" when undigestable.
inline std::string provenancePath(const std::string& digest) {
  if (digest.empty()) return "";
  return std::string(PROVENANCE_DIR) + '/' + digest;
}

// True when the path names a file the reader can open: reflowable (.epub,
// .txt, .md) or XTC (.xtc/.xtch). Anything else must never be planned, opened,
// or deleted through the shelf.
inline bool isBookPath(const std::string& path) {
  return FsHelpers::hasReflowableBookExtension(path) || FsHelpers::hasXtcExtension(path);
}

// True when a path names a file the firmware may write to on a plugin's
// instruction: inside the plugin path space and outside the reserved marker
// namespace. A target that failed this check would be a file a later firmware
// could mistake for its own bookkeeping, so every plugin-supplied file name
// passes it before reaching storage: catalog download destinations and sidecars,
// bundle entries, event-handler downloads, manifest token/config files, and the
// /api/fetch and plugin upload endpoints.
inline bool isWriteTargetAllowed(const std::string& path) {
  return protectedpaths::isPluginPath(path) && !hasMarkerSuffix(path);
}

// Surrounding whitespace/tabs a hand-edited record file could carry.
inline std::string trimRecord(const std::string& value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && static_cast<unsigned char>(value[begin]) <= ' ') begin++;
  while (end > begin && static_cast<unsigned char>(value[end - 1]) <= ' ') end--;
  return value.substr(begin, end - begin);
}

// Builds the shelf path for one download filename under the manifest's own
// destination directory. Returns "" when the directory is missing, is the SD
// root, or fails the plugin-path checks; when the filename is not a single
// safe component; or when the result is not a book file.
inline std::string plannedPath(const std::string& manifestDestDir, const std::string& filename) {
  const std::string dir = FsHelpers::canonicalFolderPath(manifestDestDir);
  if (dir.empty() || !protectedpaths::isPluginPath(dir)) return "";
  if (!FsHelpers::isSafePathComponent(std::string_view{filename}) || filename.front() == '.') return "";
  const std::string path = dir + "/" + filename;
  if (!protectedpaths::isPluginPath(path) || !isBookPath(path)) return "";
  return path;
}

// One directory up, clamped to a locked root: the shelf folder the home screen
// opens must never climb above itself ("/books/ybx/Sub" -> "/books/ybx",
// "/books/ybx" -> unchanged). Both arguments are canonical paths, "/" for the SD
// root; a value outside the root (or a trailing-slash spelling of it) returns
// the root instead of the parent.
inline std::string parentWithinRoot(const std::string& path, const std::string& root) {
  const std::string parent = FsHelpers::extractFolderPath(path);
  if (root == "/" || parent == root) return parent;
  if (parent.size() > root.size() && parent[root.size()] == '/' && parent.compare(0, root.size(), root) == 0) {
    return parent;
  }
  return root;
}

// The sidecar a shelf removal may delete, from the path downloadBook recorded
// as written for this book. Returns "" unless the recorded value is a ".json"
// file that sits directly in the manifest's own dest_dir: a manifest that names
// a settings file, another plugin's book, or a nested folder then removes
// nothing, because only the writer knows what it actually created.
inline std::string safeSidecarPath(const std::string& manifestDestDir, const std::string& recorded) {
  const std::string dir = FsHelpers::canonicalFolderPath(manifestDestDir);
  const std::string sidecar = trimRecord(recorded);
  if (dir.empty() || sidecar.empty() || !endsWithIgnoreCase(sidecar, ".json") || hasMarkerSuffix(sidecar)) return "";
  if (sidecar.size() <= dir.size() + 1 || sidecar[dir.size()] != '/' || sidecar.compare(0, dir.size(), dir) != 0) {
    return "";
  }
  const std::string name = sidecar.substr(dir.size() + 1);
  if (!FsHelpers::isSafePathComponent(std::string_view{name}) || name.front() == '.') return "";
  if (!protectedpaths::isPluginPath(sidecar)) return "";
  return sidecar;
}

}  // namespace pluginshelf
