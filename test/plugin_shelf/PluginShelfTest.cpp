#include <gtest/gtest.h>

#include <string>

#include "util/PluginShelf.h"

namespace {

using pluginshelf::isBookPath;
using pluginshelf::markerPath;
using pluginshelf::plannedPath;

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

TEST(PluginShelfMarker, NamesACompanionFileThatIsNeverABookItself) {
  EXPECT_EQ(markerPath("/books/ybx/a.epub"), "/books/ybx/a.epub.downloaded");
  EXPECT_FALSE(isBookPath(markerPath("/books/ybx/a.epub")));
}

}  // namespace
