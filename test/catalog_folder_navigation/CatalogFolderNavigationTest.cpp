#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/CatalogFolderNavigation.h"

namespace {
struct Entry {
  std::string id, parent, title, url;
};
}  // namespace

TEST(CatalogFolderNavigation, ParentGroupsAndLeavesHaveStableIndexes) {
  const std::vector<Entry> items{
      {"all", "", "Все", ""},
      {"", "all", "Все книги", "/catalog/all?kind=all"},
      {"", "all", "Читаю", "/catalog/all?kind=reading"},
      {"yandex", "", "Яндекс Книги", ""},
      {"", "yandex", "Все книги", "/catalog?source=yandex"},
      {"", "", "ЛитРес", "/litres/catalog"},
  };
  EXPECT_EQ(catalogfolders::visibleCount(items, ""), 3);
  EXPECT_EQ(catalogfolders::indexAt(items, "", 0), 0);
  EXPECT_EQ(catalogfolders::indexAt(items, "", 1), 3);
  EXPECT_EQ(catalogfolders::indexAt(items, "", 2), 5);
  EXPECT_EQ(catalogfolders::indexAt(items, "", 3), -1);
  EXPECT_EQ(catalogfolders::visibleCount(items, "all"), 2);
  EXPECT_EQ(catalogfolders::indexAt(items, "all", 0), 1);
  EXPECT_EQ(catalogfolders::indexAt(items, "all", 1), 2);
  EXPECT_EQ(catalogfolders::indexAt(items, "yandex", 0), 4);
}

TEST(CatalogFolderNavigation, SingleLinkedProviderSkipsRedundantRoot) {
  const std::vector<Entry> onlyYandex{
      {"yandex", "", "Яндекс Книги", ""},
      {"", "yandex", "Читаю", "/catalog?reading"},
      {"", "yandex", "Все книги", "/catalog?all"},
  };
  EXPECT_EQ(catalogfolders::visibleCount(onlyYandex, ""), 1);
  EXPECT_EQ(catalogfolders::visibleCount(onlyYandex, "yandex"), 2);
  const std::vector<Entry> onlyLitres{{"", "", "ЛитРес", "/litres/catalog"}};
  EXPECT_EQ(catalogfolders::visibleCount(onlyLitres, ""), 1);
  EXPECT_EQ(catalogfolders::indexAt(onlyLitres, "", 0), 0);
  EXPECT_EQ(catalogfolders::indexAt(onlyLitres, "", -1), -1);
}

TEST(CatalogFolderNavigation, RejectsCyclesAndExcessiveDepth) {
  std::vector<std::string> trail{"all", "series"};
  EXPECT_FALSE(catalogfolders::canEnter(trail, "all"));
  EXPECT_FALSE(catalogfolders::canEnter(trail, "series"));
  EXPECT_FALSE(catalogfolders::canEnter(trail, ""));
  EXPECT_TRUE(catalogfolders::canEnter(trail, "series-detail"));
  trail = {"0", "1", "2", "3", "4", "5", "6", "7"};
  EXPECT_FALSE(catalogfolders::canEnter(trail, "8"));
}

TEST(CatalogFolderNavigation, AuthenticatedJsonFolderLinksMustBeHttpsSameOrigin) {
  const std::string current = "https://my.ybx.app/api/v1/crosspoint/catalog/all?kind=series&page=1";
  EXPECT_TRUE(catalogfolders::canFollowAuthenticatedFolder(
      current, "https://my.ybx.app/api/v1/crosspoint/catalog/all?kind=series-books&series=abc"));
  EXPECT_TRUE(catalogfolders::canFollowAuthenticatedFolder(
      current, "https://MY.YBX.APP:443/api/v1/crosspoint/catalog/all?kind=shelves"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder(current, "http://my.ybx.app/catalog/all"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder(current, "https://my.ybx.app.evil.example/fetch"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder(current, "https://evil.example/catalog"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder(current, "https://my.ybx.app:8443/catalog"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder(current, "/catalog/relative"));
  EXPECT_FALSE(catalogfolders::canFollowAuthenticatedFolder("http://my.ybx.app/catalog", "http://my.ybx.app/folder"));
}
