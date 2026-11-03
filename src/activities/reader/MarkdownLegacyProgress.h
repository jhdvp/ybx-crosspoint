#pragma once

#include <HalStorage.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace markdownlegacy {

// A former .md book cache may contain plain-text page numbers and text
// offsets which no longer describe the formatted Markdown V2 layout.
// Preserve *the original bytes* before the V2 metadata/section rebuild.
// This backup is user-recoverable; the approximation is NOT an exact
// conversion of a source text anchor.
inline constexpr char kBackupSuffix[] = "/progress.md-v1.bin";
inline constexpr uint32_t kApproximateMaxSourceBytes = 64 * 1024;

enum class Action : uint8_t {
  None,          // No legacy cache or no progress file
  Approximate,   // Verified backup + usable old page fraction
  Restart,       // Verified backup, but safe ratio is not available/too expensive
  Failed         // Refuse to replace legacy cache without a verified backup
};

struct Plan {
  Action action = Action::None;
  uint16_t savedPage = 0;
  uint16_t savedTotal = 0;

  float fraction() const {
    if (savedTotal <= 1) return 0.0f;
    const auto clampedPage = savedPage < savedTotal ? savedPage : static_cast<uint16_t>(savedTotal - 1);
    return static_cast<float>(clampedPage) / static_cast<float>(savedTotal);
  }
};

struct Bytes {
  uint8_t data[10] = {};
  size_t size = 0;
};

inline bool sameBytes(const Bytes& lhs, const Bytes& rhs) {
  return lhs.size == rhs.size && std::memcmp(lhs.data, rhs.data, lhs.size) == 0;
}

inline bool readBytes(const std::string& filename, Bytes& out) {
  HalFile file;
  if (!Storage.openFileForRead("MDP", filename, file)) return false;
  const size_t length = file.size();
  if ((length != 4 && length != 6 && length != 10)) {
    file.close();
    return false;
  }
  Bytes candidate;
  candidate.size = length;
  const int count = file.read(candidate.data, candidate.size);
  const bool closed = file.close();
  if (!closed || count != static_cast<int>(candidate.size)) return false;
  out = candidate;
  return true;
}

inline bool preserve(const std::string& cachePath, const Bytes& legacy) {
  const std::string backup = cachePath + kBackupSuffix;
  if (Storage.exists(backup.c_str())) {
    Bytes existing;
    // Never replace a pre-existing snapshot; a differing copy may indicate
    // an interrupted earlier migration, external update or SD corruption.
    return readBytes(backup, existing) && sameBytes(existing, legacy);
  }
  const std::string tmp = backup + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite("MDP", tmp, f)) return false;
  const bool wrote = f.write(legacy.data, legacy.size) == legacy.size;
  f.flush();
  const bool closed = f.close();
  if (!wrote || !closed) return false;

  Bytes verified;
  if (!readBytes(tmp, verified) || !sameBytes(verified, legacy)) return false;
  if (Storage.exists(backup.c_str())) return false;
  return Storage.rename(tmp.c_str(), backup.c_str());
}

// This runs BEFORE Epub::load() invalidates the V1 TOC cache and sections.
// Presence of V2's successful index sidecar means any saved progress is
// already a V2 reading position and should be handled by the existing path.
inline Plan prepare(const std::string& cachePath, uint32_t sourceBytes) {
  const std::string legacyIndex = cachePath + "/book.bin";
  const std::string v2Index = cachePath + "/markdown-index-v2.fnv";
  const std::string progressFile = cachePath + "/progress.bin";
  if (!Storage.exists(legacyIndex.c_str()) || Storage.exists(v2Index.c_str()) ||
      !Storage.exists(progressFile.c_str())) {
    return {};
  }
  Bytes data;
  if (!readBytes(progressFile, data) || !preserve(cachePath, data)) return {Action::Failed};

  Plan plan{Action::Restart};
  if (data.size >= 6) {
    plan.savedPage = static_cast<uint16_t>(data.data[2] | (data.data[3] << 8));
    plan.savedTotal = static_cast<uint16_t>(data.data[4] | (data.data[5] << 8));
  }
  if (data.size >= 6 && plan.savedTotal > 1 && plan.savedPage != UINT16_MAX &&
      sourceBytes <= kApproximateMaxSourceBytes && plan.savedPage > 0) {
    plan.action = Action::Approximate;
  }
  return plan;
}

}  // namespace markdownlegacy
