#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pluginevents {

// A reset during append may leave a partial final JSON line. Do not glue the
// next event to it. Only inspect one bounded event; larger damage needs repair.
template <typename File>
bool repairQueueTail(File& file, const size_t maxEventLine) {
  const size_t size = file.fileSize();
  if (size == 0) return true;
  size_t end = size;
  const size_t lower = size > maxEventLine ? size - maxEventLine : 0;
  uint8_t block[128];
  while (end > lower) {
    const size_t count = std::min(sizeof(block), end - lower);
    const size_t begin = end - count;
    if (!file.seek(begin) || file.read(block, count) != static_cast<int>(count)) return false;
    for (size_t i = count; i > 0; --i) {
      if (block[i - 1] == '\n') {
        const size_t complete = begin + i;
        return complete == size || file.truncate(complete);
      }
    }
    end = begin;
  }
  return lower == 0 && file.truncate(0);
}

// The old queue remains authoritative until the complete replacement is named
// at path. A failed rename leaves the backup available for the next recovery.
template <typename Files>
bool recoverQueue(Files& files, const std::string& path) {
  const std::string backup = path + ".bak";
  const std::string pending = path + ".pending";
  if (!files.exists(path) && files.exists(backup) && !files.rename(backup, path)) return false;
  if (files.exists(backup) && !files.remove(backup)) return false;
  // An uncommitted pending file may be a short write. Never promote it.
  return !files.exists(pending) || files.remove(pending);
}

template <typename Files>
bool replaceQueue(Files& files, const std::string& path, const std::string_view content) {
  if (!recoverQueue(files, path)) return false;
  const std::string pending = path + ".pending";
  const std::string backup = path + ".bak";
  if (!files.writeComplete(pending, content)) return false;
  const bool hadQueue = files.exists(path);
  if (hadQueue && !files.rename(path, backup)) return false;
  if (!files.rename(pending, path)) {
    if (hadQueue) files.rename(backup, path);
    return false;
  }
  // The new path is authoritative even if backup cleanup fails. Recovery
  // must complete cleanup before another append or replacement is allowed.
  return !hadQueue || files.remove(backup);
}

}  // namespace pluginevents
