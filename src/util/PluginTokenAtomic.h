#pragma once

#include <string>
#include <utility>

// Crash-safe promotion of a new device token on SD (not cryptographic
// encryption; the existing plugin protected path checks still apply).
// A backup is authoritative only if the canonical file is missing.
// An uncommitted .pending file is NEVER promoted on boot.
namespace plugintoken {

template <typename Files>
bool recover(Files& files, const std::string& path) {
  const std::string backup = path + ".bak";
  const std::string pending = path + ".pending";
  if (!files.exists(path) && files.exists(backup) && !files.rename(backup, path)) return false;
  if (files.exists(path) && files.exists(backup) && !files.remove(backup)) return false;
  if (files.exists(pending) && !files.remove(pending)) return false;
  return true;
}

template <typename Files, typename StagedWriter>
bool replace(Files& files, const std::string& path, StagedWriter writePending) {
  if (path.empty() || !recover(files, path)) return false;
  const std::string backup = path + ".bak";
  const std::string pending = path + ".pending";
  if (!writePending(pending)) {
    if (files.exists(pending)) files.remove(pending);
    return false;
  }
  if (!files.exists(pending)) return false;
  const bool hadToken = files.exists(path);
  if (hadToken && !files.rename(path, backup)) {
    files.remove(pending);
    return false;
  }
  if (!files.rename(pending, path)) {
    // If power fails in this interval, recover() restores the backup.
    if (hadToken) files.rename(backup, path);
    return false;
  }
  // New canonical file is now committed; failed backup cleanup is retried
  // by the next load through recover(), never restored over the new token.
  if (hadToken) files.remove(backup);
  return true;
}

}  // namespace plugintoken
