#include <gtest/gtest.h>

#include <string>

#include "util/PluginShelf.h"

namespace {

using pluginshelf::hasMarkerSuffix;
using pluginshelf::isBookPath;
using pluginshelf::isWriteTargetAllowed;
using pluginshelf::parentWithinRoot;
using pluginshelf::plannedPath;
using pluginshelf::provenancePath;
using pluginshelf::safeSidecarPath;
using pluginshelf::trimRecord;

TEST(PluginShelfPlannedPath, AcceptsBookFilesDirectlyUnderManifestDir) {
  EXPECT_EQ(plannedPath("/books/ybx", "123 - Title.epub"), "/books/ybx/123 - Title.epub");
  EXPECT_EQ(plannedPath("/books/ybx", "book.TXT"), "/books/ybx/book.TXT");
  EXPECT_EQ(plannedPath("/books/ybx/", "book.md"), "/books/ybx/book.md");
  EXPECT_EQ(plannedPath("/books/ybx", "comic.xtc"), "/books/ybx/comic.xtc");
  EXPECT_EQ(plannedPath("/books/ybx", "comic.XTCH"), "/books/ybx/comic.XTCH");
}

TEST(PluginShelfPlannedPath, RejectsMissingRootAndRelativeDestDirs) {
  EXPECT_EQ(plannedPath("", "book.epub"), "");
  EXPECT_EQ(plannedPath("/", "book.epub"), "");
  EXPECT_EQ(plannedPath("//", "book.epub"), "");
  EXPECT_EQ(plannedPath("books/ybx", "book.epub"), "");
}

TEST(PluginShelfPlannedPath, RejectsTraversalAndNonSingleComponentNames) {
  EXPECT_EQ(plannedPath("/books/ybx", ""), "");
  EXPECT_EQ(plannedPath("/books/ybx", "sub/book.epub"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "../book.epub"), "");
  EXPECT_EQ(plannedPath("/books/ybx", ".."), "");
  EXPECT_EQ(plannedPath("/books/ybx", "a\\..b.epub"), "");
  EXPECT_EQ(plannedPath("/books/ybx", ".hidden.epub"), "");
}

TEST(PluginShelfPlannedPath, RejectsNonBookExtensions) {
  // A manifest pointing dest_dir at another app's or the firmware's own data
  // files must still never plan (and therefore never delete) them.
  EXPECT_EQ(plannedPath("/.crosspoint", "settings.json"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "book.zip"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "book.pdf"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "book.epub.bak"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "noext"), "");
}

TEST(PluginShelfPlannedPath, RejectsCredentialStoreAndAliasedPaths) {
  EXPECT_EQ(plannedPath("/.crosspoint/wifi.json", "book.epub"), "");
  EXPECT_EQ(plannedPath("/CROSSP~1", "book.epub"), "");
  EXPECT_EQ(plannedPath("/books/ybx", "koreader.json.epub"), "/books/ybx/koreader.json.epub");
}

TEST(PluginShelfIsBookPath, MatchesReflowableAndXtcOnly) {
  EXPECT_TRUE(isBookPath("/books/ybx/a.epub"));
  EXPECT_TRUE(isBookPath("A.EPUB"));
  EXPECT_TRUE(isBookPath("a.txt"));
  EXPECT_TRUE(isBookPath("a.md"));
  EXPECT_TRUE(isBookPath("a.xtc"));
  EXPECT_TRUE(isBookPath("a.XTCH"));
  EXPECT_FALSE(isBookPath("a.zip"));
  EXPECT_FALSE(isBookPath("a.png"));
  EXPECT_FALSE(isBookPath("a.epub.downloaded"));
  EXPECT_FALSE(isBookPath(""));
}

// --- finding 2: a name-only marker in the download folder is forgeable ------

TEST(PluginShelfMarkerSuffix, RejectsTargetsInTheMarkerNamespace) {
  EXPECT_TRUE(hasMarkerSuffix("/books/ybx/user.epub.downloaded"));
  // FAT names are case-insensitive, so the forged spelling is not a way past.
  EXPECT_TRUE(hasMarkerSuffix("/books/ybx/user.epub.DOWNLOADED"));
  EXPECT_TRUE(hasMarkerSuffix("user.epub.downloaded"));
  EXPECT_FALSE(hasMarkerSuffix("/books/ybx/user.epub"));
  EXPECT_FALSE(hasMarkerSuffix("/books/ybx/user.epub.meta.json"));
  EXPECT_FALSE(hasMarkerSuffix("loaded"));
  EXPECT_FALSE(hasMarkerSuffix(""));
}

TEST(PluginShelfWriteTarget, KeepsTheMarkerNamespaceOutOfPluginWrites) {
  // A manifest that names its sidecar or bundle entry "<user book>.downloaded"
  // would plant a file shaped like the firmware's own marker, so the write
  // target gate rejects it while accepting the documented conventions.
  EXPECT_FALSE(isWriteTargetAllowed("/books/ybx/user.epub.downloaded"));
  EXPECT_FALSE(isWriteTargetAllowed("/books/ybx/user.epub.DoWnLoAdEd"));
  EXPECT_FALSE(isWriteTargetAllowed("/plugins/ybx/assets/notes.downloaded"));
  EXPECT_TRUE(isWriteTargetAllowed("/books/ybx/a.epub.meta.json"));
  EXPECT_TRUE(isWriteTargetAllowed("/plugins/ybx/assets/icon.bin"));
  // Credential stores, the record folder and traversal stay unwritable.
  EXPECT_FALSE(isWriteTargetAllowed("/.crosspoint/koreader.json"));
  EXPECT_FALSE(isWriteTargetAllowed("/.crosspoint/shelf/d41d8cd98f00b204e9800998ecf8427e"));
  EXPECT_FALSE(isWriteTargetAllowed("/books/ybx/../a.json"));
  EXPECT_FALSE(isWriteTargetAllowed("books/ybx/a.json"));
}

TEST(PluginShelfProvenance, IsOutsideEveryPathAPluginCanWrite) {
  const std::string record = provenancePath("d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(record, "/.crosspoint/shelf/d41d8cd98f00b204e9800998ecf8427e");
  // Download, sidecar and bundle targets all pass isWriteTargetAllowed, which
  // fails a record path (and a marker name), so a manifest cannot stamp
  // provenance for a file it never downloaded (dest_dir "/books/ybx", sidecar
  // "<user file>.downloaded").
  EXPECT_FALSE(isWriteTargetAllowed(record));
  EXPECT_FALSE(protectedpaths::isPluginPath(record));
  EXPECT_TRUE(protectedpaths::isSensitivePath(record.c_str()));
  // A manifest cannot name the record folder as its download tree either.
  EXPECT_EQ(plannedPath(pluginshelf::PROVENANCE_DIR, "user.epub"), "");
  EXPECT_EQ(provenancePath(""), "");
}

// --- finding 1: removal may delete only what the download recorded ----------

TEST(PluginShelfSidecar, DeletesOnlyAJsonFileInTheManifestsOwnFolder) {
  // The documented "{dest}.meta.json" convention.
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/a.epub.meta.json"), "/books/ybx/a.epub.meta.json");
  EXPECT_EQ(safeSidecarPath("/books/ybx/", " /books/ybx/a.epub.meta.json\r\n"), "/books/ybx/a.epub.meta.json");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/d41d8cd98f00b204e9800998ecf8427e.json"),
            "/books/ybx/d41d8cd98f00b204e9800998ecf8427e.json");
}

TEST(PluginShelfSidecar, RefusesTheRecordedSettingsFileAttack) {
  // device.json: "sidecar": {"path": "/.crosspoint/settings.json"} - with no
  // provenance record of a write, removing a shelf book must delete nothing.
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/.crosspoint/settings.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/.crosspoint/wifi.json"), "");
  // Out of the manifest tree, into a sibling folder, or above the card.
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/library.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/sub/a.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/../a.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "books/ybx/a.json"), "");
  EXPECT_EQ(safeSidecarPath("", "/books/ybx/a.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", ""), "");
  // Same folder but not a sidecar the shelf can own.
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/a.epub"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/a.json.bak"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/.hidden.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx/a.json.downloaded"), "");
  // A near-miss prefix is not inside the folder.
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx-probe/a.json"), "");
  EXPECT_EQ(safeSidecarPath("/books/ybx", "/books/ybx"), "");
}

TEST(PluginShelfTrimRecord, DropsWhitespaceAroundARecordedPath) {
  EXPECT_EQ(trimRecord("  /books/ybx/a.json\t\r\n"), "/books/ybx/a.json");
  EXPECT_EQ(trimRecord(""), "");
  EXPECT_EQ(trimRecord("   "), "");
}

// --- finding 3: one canonical spelling for the locked shelf folder ----------

TEST(PluginShelfParentWithinRoot, NeverClimbsAboveTheLockedFolder) {
  EXPECT_EQ(parentWithinRoot("/books/ybx/Sub", "/books/ybx"), "/books/ybx");
  EXPECT_EQ(parentWithinRoot("/books/ybx/Sub/Deep", "/books/ybx"), "/books/ybx/Sub");
  // At the root, and with either trailing-slash spelling, Back stays put.
  EXPECT_EQ(parentWithinRoot("/books/ybx", "/books/ybx"), "/books/ybx");
  EXPECT_EQ(parentWithinRoot("/books/ybx/", "/books/ybx"), "/books/ybx");
  // A value that is not inside the root returns to the root instead of above it.
  EXPECT_EQ(parentWithinRoot("/books", "/books/ybx"), "/books/ybx");
  EXPECT_EQ(parentWithinRoot("/books/ybxtoken/x", "/books/ybx"), "/books/ybx");
  EXPECT_EQ(parentWithinRoot("/other/x", "/books/ybx"), "/books/ybx");
  // Unlocked browser: one directory up, home at the SD root.
  EXPECT_EQ(parentWithinRoot("/books/x", "/"), "/books");
  EXPECT_EQ(parentWithinRoot("/", "/"), "/");
}

}  // namespace
