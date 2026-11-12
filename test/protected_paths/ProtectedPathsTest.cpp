#include <gtest/gtest.h>

#include "network/ProtectedPaths.h"

using protectedpaths::isPluginPath;
using protectedpaths::isSensitivePath;

TEST(ProtectedPaths, CredentialStoresAreSensitive) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/opds.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/koreader.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/ybx-account.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/ybx-account.json.tmp"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/ybx-account.json.bak"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/ybx/reading-summary.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/ybx/achievements.json"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/YBX-AC~1.JSO"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json.tmp"));
  EXPECT_TRUE(isSensitivePath("/.CrossPoint/WIFI.json"));
}

// The catalog shelf's provenance records decide which files removal may delete,
// so no manifest-chosen download/sidecar/bundle write may create one.
TEST(ProtectedPaths, ShelfProvenanceRecordsAreSensitive) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/shelf/d41d8cd98f00b204e9800998ecf8427e"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/shelf.json"));
  EXPECT_TRUE(isSensitivePath("/.CrossPoint/SHELF/abc.tmp"));
  EXPECT_TRUE(isSensitivePath("//.crosspoint//shelf/./abc"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/plugins/shelf-helper/device.json"));
  EXPECT_FALSE(isPluginPath("/.crosspoint/shelf/forged.epub"));
}

TEST(ProtectedPaths, SpellingVariantsCannotDodgeTheMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/./wifi.json"));
  EXPECT_TRUE(isSensitivePath("//.crosspoint//wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_TRUE(isSensitivePath(".crosspoint/opds.json"));
}

TEST(ProtectedPaths, NamesSdFatWouldTrimOrAliasStillMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json."));
  EXPECT_TRUE(isSensitivePath("/.crosspoint./wifi.json"));
  EXPECT_TRUE(isSensitivePath("/ .crosspoint/ wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint ./opds.json"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/WIFI~1.JSO"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/WIFI~1.JSO"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1"));                 // WebDAV PROPFIND would list the stores
  EXPECT_FALSE(isSensitivePath("/Books/Photos/IMG~1.JPG"));  // too deep to reach a store
  EXPECT_FALSE(isSensitivePath("/Books/a~b.epub"));
}

TEST(ProtectedPaths, EverythingElseStaysReachable) {
  EXPECT_FALSE(isSensitivePath("/.crosspoint/settings.json"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/library.idx"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/month-wallpaper.json"));
  EXPECT_FALSE(isSensitivePath("/.fonts/Noto/Noto_14.cpfont"));
  EXPECT_FALSE(isSensitivePath("/Books/wifi.json"));
}

TEST(ProtectedPaths, PluginPathRules) {
  EXPECT_FALSE(isPluginPath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_FALSE(isPluginPath("/.crosspoint/./koreader.json"));
  EXPECT_FALSE(isPluginPath("/.crosspoint/ybx-account.json"));
  EXPECT_FALSE(isPluginPath("relative/path"));
  EXPECT_FALSE(isPluginPath("/"));
  EXPECT_TRUE(isPluginPath("/.crosspoint/month-wallpaper.json"));
  EXPECT_TRUE(isPluginPath("/Books/new.epub"));
}
