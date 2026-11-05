// Host-side rules for the OTA path: which release feed a device may consult,
// which asset URL and asset name it may follow, how the RC/stable channels
// compare, and what a downloaded body must look like before the inactive OTA
// partition can become the boot target. See src/network/OtaReleasePolicy.h.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "src/network/OtaReleasePolicy.h"

namespace {

std::string suffixFor(const char* boardName) {
  char suffix[32] = {};
  if (!ota_policy::assetSuffixForBoard(boardName, strlen(boardName), suffix, sizeof(suffix))) return "<rejected>";
  return suffix;
}

std::string nameFor(const char* tag, const char* boardName) {
  char name[48] = {};  // ReleaseJsonParser::kFirmwareAssetNameSize
  if (!ota_policy::appAssetName(tag, boardName, strlen(boardName), name, sizeof(name))) return "<rejected>";
  return name;
}

// A minimal ESP image header: magic at 0, chip_id at 12.
std::string espImagePrefix(uint8_t magic, uint16_t chipId) {
  std::string bytes(ota_policy::kEspImageChipIdReadLen, '\0');
  bytes[0] = static_cast<char>(magic);
  memcpy(&bytes[ota_policy::kEspImageChipIdOffset], &chipId, sizeof(chipId));
  return bytes;
}

}  // namespace

// --- Rule: updates come from the YBX fork's releases only -------------------

TEST(OtaReleasePolicy, FeedIsTheForkLatestRelease) {
  EXPECT_STREQ(ota_policy::kReleaseFeedUrl,
               "https://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest");
  EXPECT_TRUE(ota_policy::releaseFeedAllowed(ota_policy::kReleaseFeedUrl));
}

TEST(OtaReleasePolicy, UpstreamAndOtherFeedUrlsAreRefused) {
  EXPECT_FALSE(ota_policy::releaseFeedAllowed("https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest"));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed("http://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest"));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed("https://api.github.com/repos/jhdvp/ybx-crosspoint/releases"));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed("https://api.github.com/repos/jhdvp/other/releases/latest"));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed("https://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest?x=1"));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed(nullptr));
  EXPECT_FALSE(ota_policy::releaseFeedAllowed(""));
}

TEST(OtaReleasePolicy, AssetDownloadStaysOnThisForkReleaseHosts) {
  EXPECT_TRUE(ota_policy::assetDownloadAllowed(
      "https://github.com/jhdvp/ybx-crosspoint/releases/download/2.0.2/ybx-2.0.2-x3-x4.bin"));
  EXPECT_TRUE(ota_policy::assetDownloadAllowed(
      "https://objects.githubusercontent.com/github-production-release-asset-2e65be/123/abc?sha=deadbeef"));
}

TEST(OtaReleasePolicy, AssetDownloadRejectsOtherOrigins) {
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "http://github.com/jhdvp/ybx-crosspoint/releases/download/2.0.2/ybx-2.0.2-x3-x4.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com/crosspoint-reader/crosspoint-reader/releases/download/2.0.2/firmware.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com/jhdvp/other/releases/download/2.0.2/ybx-2.0.2-x3-x4.bin"));
  // Same repo name, different path: the API URL is not an asset download.
  EXPECT_FALSE(ota_policy::assetDownloadAllowed("https://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest"));
  // Port, path prefix and look-alike hosts must not pass the host check.
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com:8443/jhdvp/ybx-crosspoint/releases/download/2.0.2/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com.example.com/jhdvp/ybx-crosspoint/releases/download/2.0.2/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://example.com/github.com/jhdvp/ybx-crosspoint/releases/download/2.0.2/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com/jhdvp/ybx-crosspoint.evil/releases/download/2.0.2/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(
      "https://github.com/jhdvp/ybx-crosspoint/releases/downloaded/2.0.2/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed("https://objects.githubusercontent.com.evil.com/a.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed("https://example.com/ybx-2.0.2-x3-x4.bin"));
  EXPECT_FALSE(ota_policy::assetDownloadAllowed(nullptr));
}

// --- Rule: a missing release is not an error ---------------------------------

TEST(OtaReleasePolicy, OnlyNotFoundMeansNoUpdate) {
  EXPECT_TRUE(ota_policy::missingReleaseFromStatus(404));
  EXPECT_FALSE(ota_policy::missingReleaseFromStatus(200));
  EXPECT_FALSE(ota_policy::missingReleaseFromStatus(403));
  EXPECT_FALSE(ota_policy::missingReleaseFromStatus(429));
  EXPECT_FALSE(ota_policy::missingReleaseFromStatus(500));
  // 0 is "no response at all" (DNS, connect, TLS): an error the reader should see.
  EXPECT_FALSE(ota_policy::missingReleaseFromStatus(0));
}

// --- Rule: tag, board and asset naming --------------------------------------

TEST(OtaReleasePolicy, VersionTagLosesOptionalVPrefix) {
  char version[32] = {};
  ASSERT_TRUE(ota_policy::normalizeVersionTag("v2.0.2", version, sizeof(version)));
  EXPECT_STREQ(version, "2.0.2");
  ASSERT_TRUE(ota_policy::normalizeVersionTag("V2.0.2-rc+a1b2c3d", version, sizeof(version)));
  EXPECT_STREQ(version, "2.0.2-rc+a1b2c3d");
  ASSERT_TRUE(ota_policy::normalizeVersionTag("2.0.2", version, sizeof(version)));
  EXPECT_STREQ(version, "2.0.2");
  // A 'v' that is not a tag prefix stays part of the string.
  ASSERT_TRUE(ota_policy::normalizeVersionTag("vehicle", version, sizeof(version)));
  EXPECT_STREQ(version, "vehicle");
}

TEST(OtaReleasePolicy, VersionTagRejectsEmptyAndOverflow) {
  char version[8] = {};
  EXPECT_FALSE(ota_policy::normalizeVersionTag(nullptr, version, sizeof(version)));
  EXPECT_FALSE(ota_policy::normalizeVersionTag("", version, sizeof(version)));
  EXPECT_FALSE(ota_policy::normalizeVersionTag("2.0.2-rc+abcdefgh", version, sizeof(version)));
  EXPECT_EQ(version[0], '\0');
}

TEST(OtaReleasePolicy, BoardSuffixIsThePublishedAssetSuffix) {
  EXPECT_EQ(suffixFor("x4"), "x3-x4");      // combined X3/X4 ESP32-C3 image
  EXPECT_EQ(suffixFor("x4pro"), "x4pro");   // ESP32-S3 board: never shares the C3 asset
  EXPECT_EQ(suffixFor("x4c"), "x4c");
  EXPECT_EQ(suffixFor("papermono"), "papermono");
  EXPECT_EQ(suffixFor(""), "<rejected>");
  EXPECT_EQ(suffixFor("murphy_m4"), "murphy_m4");
}

TEST(OtaReleasePolicy, AppAssetNameMatchesPublishedNaming) {
  EXPECT_EQ(nameFor("2.0.2", "x4"), "ybx-2.0.2-x3-x4.bin");
  EXPECT_EQ(nameFor("v2.0.2", "x4"), "ybx-2.0.2-x3-x4.bin");
  EXPECT_EQ(nameFor("2.0.2rc", "x4pro"), "ybx-2.0.2rc-x4pro.bin");
  EXPECT_EQ(nameFor("2.0.1", "x4c"), "ybx-2.0.1-x4c.bin");
  EXPECT_NE(nameFor("2.0.2", "x4").find(".bin"), std::string::npos);
}

TEST(OtaReleasePolicy, AppAssetNameNeverNamesAFactoryImage) {
  // The OTA asset name is built from the version and this board's tag, so the
  // "-factory" file cannot be selected even when a release carries both.
  EXPECT_EQ(nameFor("2.0.2", "x4").find("factory"), std::string::npos);
  EXPECT_EQ(nameFor("2.0.2", "x4pro").find("factory"), std::string::npos);
}

TEST(OtaReleasePolicy, AppAssetNameRejectsInsteadOfTruncating) {
  char name[48] = {};  // ReleaseJsonParser::kFirmwareAssetNameSize
  EXPECT_TRUE(ota_policy::appAssetName("2.0.2", "x4", 2, name, sizeof(name)));
  EXPECT_STREQ(name, "ybx-2.0.2-x3-x4.bin");
  // 4 + 31-char version + 1 + 23-char board tag + 4 does not fit 48 bytes: the
  // call reports the miss instead of handing back a truncated name.
  EXPECT_FALSE(ota_policy::appAssetName("2.0.2-rc+aaaaaaaaaaaaaaaaaaaaaa", "metalio_eink4_papermono", 23, name,
                                        sizeof(name)));
  EXPECT_EQ(name[0], '\0');
  // A tag longer than the parser's own tagName field is rejected as well.
  EXPECT_FALSE(ota_policy::appAssetName("2.0.2-rc+aaaaaaaaaaaaaaaaaaaaaaaaaaa", "x4", 2, name, sizeof(name)));
  EXPECT_FALSE(ota_policy::appAssetName("2.0.2", "x4", 2, name, 12));
  EXPECT_FALSE(ota_policy::appAssetName("", "x4", 2, name, sizeof(name)));
}

// --- Rule: OTA carries stable builds only -----------------------------------

TEST(OtaReleasePolicy, ChannelClassification) {
  EXPECT_EQ(ota_policy::channelOf("2.0.2"), ota_policy::Channel::Stable);
  EXPECT_EQ(ota_policy::channelOf("v2.0.2"), ota_policy::Channel::Stable);
  EXPECT_EQ(ota_policy::channelOf("2.0.1rc"), ota_policy::Channel::Preview);
  EXPECT_EQ(ota_policy::channelOf("2.0.1-rc"), ota_policy::Channel::Preview);
  EXPECT_EQ(ota_policy::channelOf("2.0.1-rc+4c00b68"), ota_policy::Channel::Preview);
  EXPECT_EQ(ota_policy::channelOf("2.0.1-dev-feat-ybx-05c6cf8"), ota_policy::Channel::Preview);
  // Anything else is unknown and never treated as stable.
  EXPECT_EQ(ota_policy::channelOf("2.0.2-1"), ota_policy::Channel::Unknown);
  EXPECT_EQ(ota_policy::channelOf("2.0.2+build5"), ota_policy::Channel::Unknown);
  EXPECT_EQ(ota_policy::channelOf("2.0"), ota_policy::Channel::Unknown);
  EXPECT_EQ(ota_policy::channelOf(""), ota_policy::Channel::Unknown);
  EXPECT_EQ(ota_policy::channelOf("latest"), ota_policy::Channel::Unknown);
}

TEST(OtaReleasePolicy, StableDevicesUpgradeToHigherStableOnly) {
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.2"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1", "2.1.0"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1", "3.0.0"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1", "v2.0.2"));
  // Numeric, not lexicographic: 2.0.10 is newer than 2.0.9.
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.9", "2.0.10"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.10", "2.0.9"));
}

TEST(OtaReleasePolicy, StableDevicesNeverReceiveAPreviewBuild) {
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.1rc"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.2rc"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "3.0.0-rc+abcdef1"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.2-dev-topic-abc1234"));
  // Unclassifiable suffixes are not stable either.
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.2-1"));
}

TEST(OtaReleasePolicy, PreviewDevicesAdvanceToStableOfSameOrNewerCore) {
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1rc", "2.0.1"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1-rc+4c00b68e4d", "2.0.1"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1-rc+4c00b68e4d", "2.0.2"));
  EXPECT_TRUE(ota_policy::updateOfferedIsNewer("2.0.1-dev-topic-05c6cf8", "2.0.1"));
}

TEST(OtaReleasePolicy, NoDowngradeAndNoSameVersion) {
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.2", "2.0.1"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.2rc", "2.0.1"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.1"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1rc", "2.0.1rc"));
}

TEST(OtaReleasePolicy, PreviewToPreviewIsNotAnOtaUpdate) {
  // Newer preview builds are installed from the SD card, not over OTA.
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1rc", "2.0.2rc"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1-rc+4c00b68", "2.0.1-rc+9999abc"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1-dev-a-1111111", "2.0.2-dev-b-2222222"));
}

TEST(OtaReleasePolicy, UnparsableVersionsAreNeverNewer) {
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("garbage", "2.0.2"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "garbage"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("", "2.0.2"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", ""));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", nullptr));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer(nullptr, "2.0.2"));
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "2.0.2.3"));
  // Overflowing number fields are rejected instead of wrapping.
  EXPECT_FALSE(ota_policy::updateOfferedIsNewer("2.0.1", "9999999999999.0.0"));
}

// --- Rule: the downloaded body must be a complete app image -----------------

TEST(OtaReleasePolicy, MinimumAppImageSizeMatchesTheSdCardFloor) {
  EXPECT_EQ(ota_policy::kMinAppImageSize, 64u * 1024u);
}

TEST(OtaReleasePolicy, EspImageMagicNeedsTheWholeChipIdField) {
  const std::string good = espImagePrefix(ota_policy::kEspImageMagic, 5);
  EXPECT_TRUE(ota_policy::hasEspImageMagic(reinterpret_cast<const uint8_t*>(good.data()), good.size()));
  // An HTML error page starts with '<'.
  const std::string html = espImagePrefix('<', 5);
  EXPECT_FALSE(ota_policy::hasEspImageMagic(reinterpret_cast<const uint8_t*>(html.data()), html.size()));
  EXPECT_FALSE(ota_policy::hasEspImageMagic(reinterpret_cast<const uint8_t*>(good.data()),
                                            good.size() - 1));  // short body: cannot judge yet
  EXPECT_FALSE(ota_policy::hasEspImageMagic(nullptr, good.size()));
  EXPECT_FALSE(ota_policy::hasEspImageMagic(reinterpret_cast<const uint8_t*>(""), 0));
}
