#pragma once

#include <cstddef>
#include <cstdint>

// Release-source, asset-naming and update-channel rules for the OTA path.
//
// Everything here is deliberately free of Arduino/ESP-IDF calls so the host
// suite (test/ota_release_policy) can cover each rule branch, and so OtaUpdater
// keeps a single place where "may this release be installed on this board?" is
// answered. OtaUpdater enforces every rule before the inactive OTA slot is made
// the boot target, so a rejected image can never be started.
namespace ota_policy {

// The only release feed a device may consult: the YBX fork's stable releases.
// Upstream CrossPoint releases are a different firmware and must never be
// offered here (see YBX_FORK.md). GitHub's /releases/latest already excludes
// drafts and prereleases, which is the first half of the channel rule;
// updateOfferedIsNewer() enforces the second half.
extern const char kReleaseFeedUrl[];

// The fork that owns those releases, as "owner/repo".
extern const char kReleaseRepo[];

// True only for the exact release feed above. Callers pass a compile-time
// constant, so this documents and pins the dependency instead of guessing.
bool releaseFeedAllowed(const char* url);

// True when `url` is a release-download URL for this fork: https on
// github.com under /<owner>/<repo>/releases/download/, or GitHub's release asset
// CDN. A release body that points the bulk transfer at any other host is refused
// before a byte is fetched. Ports and look-alike hosts are excluded by the
// required '/' or end-of-string after the host name.
bool assetDownloadAllowed(const char* url);

// A 404 from the release feed means "no published release for this board yet",
// which is not a failure the user has to act on. Any other status (including 0,
// i.e. no response at all) is an error.
bool missingReleaseFromStatus(int status);

// Strips an optional leading 'v'/'V' from a release tag ("v2.0.2" -> "2.0.2"):
// asset names and version comparisons use the numeric part only. Returns false
// when the tag is empty or does not fit `outSize`.
bool normalizeVersionTag(const char* tag, char* out, size_t outSize);

// Published asset suffix for a compiled compatibility tag (board_tag::boardName
// / boardNameLen, which are not null-terminated at the name boundary). The
// combined X3/X4 ESP32-C3 image is tagged "x4" and publishes as "x3-x4"; every
// other board publishes under its own tag ("x4pro", "x4c", ...). Returns false
// when the name is empty or does not fit.
bool assetSuffixForBoard(const char* boardName, size_t boardNameLen, char* out, size_t outSize);

// Full OTA *application* asset name for this device: "ybx-<version>-<suffix>.bin".
// A USB factory image is not a valid application image, so the release keeps it
// under a distinct name ("ybx-<version>-<suffix>-factory.bin", see
// scripts/package_release.py): ReleaseJsonParser matches asset names exactly, so
// the factory file can never be selected here — a release carrying only a factory
// image reports no update. Returns false when the name does not fit `outSize`.
bool appAssetName(const char* tag, const char* boardName, size_t boardNameLen, char* out, size_t outSize);

// Build channel. Stable = exactly MAJOR.MINOR.PATCH with no suffix. Preview =
// the documented pre-release suffixes: "rc", "-rc..." (release candidates such as
// "2.0.1-rc+4c00b68") and "-dev-..." (development builds). Anything else is
// Unknown and is never treated as stable.
enum class Channel { Stable, Preview, Unknown };
Channel channelOf(const char* version);

// The OTA channel rule, fail closed:
//  - the offered build must parse as MAJOR.MINOR.PATCH and must be Stable;
//  - a higher core version than the running build is newer, a lower one never is;
//  - at the same core version only a Preview device moves up, and only to Stable
//    (that is the RC -> stable hand-over);
//  - preview -> preview is not an OTA update: preview devices are reinstalled
//    from the SD card (YBX_FORK.md).
bool updateOfferedIsNewer(const char* currentVersion, const char* offeredVersion);

// Smallest application image accepted over OTA; matches the SD-card path's
// floor. Anything below is a truncated transfer or an error page, not firmware.
constexpr size_t kMinAppImageSize = 64u * 1024u;

// esp_image_header_t: magic byte and the chip_id field (ESP-IDF puts the target
// id at offset 12).
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr size_t kEspImageChipIdOffset = 12;
constexpr size_t kEspImageChipIdReadLen = kEspImageChipIdOffset + sizeof(uint16_t);

// True when the first `len` bytes of a downloaded body look like an ESP image.
// Requires the whole chip_id field, so a short body reports false.
bool hasEspImageMagic(const uint8_t* data, size_t len);
}  // namespace ota_policy
