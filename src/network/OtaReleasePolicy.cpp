#include "OtaReleasePolicy.h"

#include <cstring>

namespace ota_policy {

const char kReleaseFeedUrl[] = "https://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest";
const char kReleaseRepo[] = "jhdvp/ybx-crosspoint";

namespace {

constexpr size_t kVersionCapacity = 32;  // ReleaseJsonParser::tagName
constexpr size_t kSuffixCapacity = 24;    // board_tag::Scanner::MAX_NAME + 1

bool startsWith(const char* text, size_t textLen, const char* prefix) {
  const size_t prefixLen = strlen(prefix);
  return textLen >= prefixLen && memcmp(text, prefix, prefixLen) == 0;
}

bool hasPrefix(const char* text, const char* prefix) {
  return text != nullptr && strncmp(text, prefix, strlen(prefix)) == 0;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads one dot-separated number, rejecting overflow (more than 9 digits).
bool readNumber(const char*& p, int& out) {
  if (!isDigit(*p)) return false;
  long long value = 0;
  size_t digits = 0;
  while (isDigit(*p)) {
    value = value * 10 + (*p - '0');
    ++p;
    if (++digits > 9) return false;
  }
  out = static_cast<int>(value);
  return true;
}

// Parses "MAJOR.MINOR.PATCH" and leaves `p` on the first byte after it.
bool readCore(const char* version, int& major, int& minor, int& patch, const char*& suffix) {
  if (!version || version[0] == '\0') return false;
  const char* p = version;
  if (!readNumber(p, major) || *p != '.') return false;
  ++p;
  if (!readNumber(p, minor) || *p != '.') return false;
  ++p;
  if (!readNumber(p, patch)) return false;
  suffix = p;
  return true;
}

// Release tags are the numeric version with an optional leading 'v'
// (YBX_FORK.md), so a 'v' directly in front of a digit is a tag prefix.
const char* skipTagPrefix(const char* version) {
  if (version != nullptr && (version[0] == 'v' || version[0] == 'V') && isDigit(version[1])) return version + 1;
  return version;
}

Channel channelOfSuffix(const char* suffix) {
  if (suffix[0] == '\0') return Channel::Stable;
  if (suffix[0] == 'r' && suffix[1] == 'c') return Channel::Preview;  // "2.0.1rc"
  if (startsWith(suffix, strlen(suffix), "-rc")) return Channel::Preview;
  if (startsWith(suffix, strlen(suffix), "-dev-")) return Channel::Preview;
  return Channel::Unknown;
}

// Copies at most `outSize - 1` bytes and null-terminates; false when the source
// did not fit, so callers never install a silently truncated name.
bool boundedCopy(const char* src, size_t srcLen, char* out, size_t outSize) {
  if (src == nullptr || out == nullptr || outSize == 0 || srcLen >= outSize) return false;
  memcpy(out, src, srcLen);
  out[srcLen] = '\0';
  return true;
}

}  // namespace

bool releaseFeedAllowed(const char* url) { return url != nullptr && strcmp(url, kReleaseFeedUrl) == 0; }

bool assetDownloadAllowed(const char* url) {
  if (!hasPrefix(url, "https://")) return false;
  const char* host = url + strlen("https://");
  if (hasPrefix(host, "objects.githubusercontent.com/")) return true;  // GitHub release asset CDN
  if (!hasPrefix(host, "github.com/")) return false;
  // The host is exact above, so a port or a look-alike domain never gets here.
  const char* path = host + strlen("github.com/");
  return hasPrefix(path, kReleaseRepo) && hasPrefix(path + strlen(kReleaseRepo), "/releases/download/");
}

bool missingReleaseFromStatus(int status) { return status == 404; }

bool normalizeVersionTag(const char* tag, char* out, size_t outSize) {
  if (out != nullptr && outSize != 0) out[0] = '\0';
  const char* version = skipTagPrefix(tag);
  if (version == nullptr || version[0] == '\0') return false;
  return boundedCopy(version, strlen(version), out, outSize);
}

bool assetSuffixForBoard(const char* boardName, size_t boardNameLen, char* out, size_t outSize) {
  if (boardName == nullptr || boardNameLen == 0) return false;
  // The combined X3/X4 ESP32-C3 build is tagged "x4" and published as "x3-x4".
  if (boardNameLen == 2 && memcmp(boardName, "x4", 2) == 0) return boundedCopy("x3-x4", 5, out, outSize);
  return boundedCopy(boardName, boardNameLen, out, outSize);
}

bool appAssetName(const char* tag, const char* boardName, size_t boardNameLen, char* out, size_t outSize) {
  // Failed validation must not leave the previously accepted name in the buffer.
  if (out != nullptr && outSize != 0) out[0] = '\0';
  char version[kVersionCapacity];
  char suffix[kSuffixCapacity];
  if (!normalizeVersionTag(tag, version, sizeof(version))) return false;
  if (!assetSuffixForBoard(boardName, boardNameLen, suffix, sizeof(suffix))) return false;

  size_t needed = 0;
  const char* parts[] = {"ybx-", version, "-", suffix, ".bin"};
  for (const char* part : parts) needed += strlen(part);
  if (needed + 1 > outSize) return false;

  size_t written = 0;
  for (const char* part : parts) {
    const size_t len = strlen(part);
    memcpy(out + written, part, len);
    written += len;
  }
  out[written] = '\0';
  return true;
}

Channel channelOf(const char* version) {
  int major, minor, patch;
  const char* suffix = nullptr;
  if (!readCore(skipTagPrefix(version), major, minor, patch, suffix)) return Channel::Unknown;
  return channelOfSuffix(suffix);
}

bool updateOfferedIsNewer(const char* currentVersion, const char* offeredVersion) {
  int currentMajor, currentMinor, currentPatch;
  int offeredMajor, offeredMinor, offeredPatch;
  const char* currentSuffix = nullptr;
  const char* offeredSuffix = nullptr;
  if (!readCore(skipTagPrefix(currentVersion), currentMajor, currentMinor, currentPatch, currentSuffix)) return false;
  if (!readCore(skipTagPrefix(offeredVersion), offeredMajor, offeredMinor, offeredPatch, offeredSuffix)) return false;

  // The OTA channel carries stable builds only: never offer a release candidate,
  // a development build or a tag whose suffix we cannot classify.
  if (channelOfSuffix(offeredSuffix) != Channel::Stable) return false;

  if (offeredMajor != currentMajor) return offeredMajor > currentMajor;
  if (offeredMinor != currentMinor) return offeredMinor > currentMinor;
  if (offeredPatch != currentPatch) return offeredPatch > currentPatch;

  // Same core version: only a preview device advances, and only onto the stable
  // build of that version. A stable device is already on it.
  return channelOfSuffix(currentSuffix) == Channel::Preview;
}

bool hasEspImageMagic(const uint8_t* data, size_t len) {
  return data != nullptr && len >= kEspImageChipIdReadLen && data[0] == kEspImageMagic;
}

}  // namespace ota_policy
