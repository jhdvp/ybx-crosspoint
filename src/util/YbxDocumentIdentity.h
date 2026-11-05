#pragma once

#include <string>
#include <string_view>

#include "nativeapps/notes/NotesDocument.h"

// Native Notes have random immutable filenames. Their clipping sync key
// can follow the filename across content edits, but progress sync MUST keep
// the configured KoSync strategy: different note revisions can have different
// pagination and stale remote positions must not be silently applied.
namespace ybxdocument {

inline bool isManagedNote(const std::string_view path) {
  // The reader can move a completed native note into /Read and users can
  // copy it between SD directories. The random generated basename is the
  // portable identity, never the directory or mutable Markdown contents.
  if (path.empty() || path.front() != '/') return false;
  const size_t slash = path.find_last_of('/');
  if (slash == std::string_view::npos || slash + 1 == path.size()) return false;
  size_t segmentStart = 1;
  while (segmentStart <= slash) {
    size_t end = path.find('/', segmentStart);
    if (end == std::string_view::npos || end > slash) end = slash;
    const auto segment = path.substr(segmentStart, end - segmentStart);
    if (segment.empty() || segment == "." || segment == "..") return false;
    if (end == slash) break;
    segmentStart = end + 1;
  }
  return nativeapps::notes::validFileName(path.substr(slash + 1));
}

// Used ONLY to choose the clipping endpoint document key, not the
// reader-progress hash, so non-native Markdown and EPUB remain unchanged.
inline bool useStableClippingIdentity(const std::string_view path) {
  return isManagedNote(path);
}

// The callback is intentionally lazy: regular books never calculate an
// extra hash or change their KoSync document key.
template <typename FilenameHash>
std::string clippingDocumentKey(const std::string_view path, const std::string& kosyncHash,
                                FilenameHash filenameHash) {
  if (!isManagedNote(path)) return kosyncHash;
  return filenameHash();
}

}  // namespace ybxdocument
