#include <gtest/gtest.h>

#include <array>

#include "nativeapps/CatalogPolicy.h"
#include "nativeapps/AppPaths.h"

namespace {
void launch() {}
bool enabled() { return true; }
bool disabled() { return false; }
struct Entry {
  const char* id;
  void (*create)();
  bool (*supported)();
};
}  // namespace

TEST(NativeAppsCatalog, NoAppsIsValidAndSafe) {
  EXPECT_TRUE(nativeapps::catalog::valid(static_cast<const Entry*>(nullptr), 0));
  EXPECT_EQ(nativeapps::catalog::availableCount(static_cast<const Entry*>(nullptr), 0), 0u);
  EXPECT_EQ(nativeapps::catalog::availableAt(static_cast<const Entry*>(nullptr), 0, 0), nullptr);
}
TEST(NativeAppsCatalog, RejectsInvalidAndUnboundedIdentifiers) {
  for (auto id : {"", "A", "-bad", "bad-", "bad/dir", "../etc", "space id", "a_b", "123",
                  "abcdefghijklmnopqrstuvwxyz"}) {
    EXPECT_FALSE(nativeapps::catalog::validId(id)) << id;
  }
  EXPECT_FALSE(nativeapps::catalog::validId(nullptr));
  EXPECT_TRUE(nativeapps::catalog::validId("reading-notes-2"));
}
TEST(NativeAppsCatalog, RequiresFactoriesAndUniqueIds) {
  constexpr Entry missing[] = {{"memory", nullptr, nullptr}};
  constexpr Entry duplicates[] = {{"memory", launch, nullptr}, {"memory", launch, enabled}};
  EXPECT_FALSE(nativeapps::catalog::valid(missing, 1));
  EXPECT_FALSE(nativeapps::catalog::valid(duplicates, 2));
}
TEST(NativeAppsCatalog, FiltersUnsupportedWithoutShiftingUnexpectedOrder) {
  const Entry items[] = {{"memory", launch, enabled}, {"hidden", launch, disabled},
                         {"notes", launch, nullptr}, {"unsupported", launch, disabled}};
  ASSERT_TRUE(nativeapps::catalog::valid(items, 4));
  EXPECT_EQ(nativeapps::catalog::availableCount(items, 4), 2u);
  ASSERT_NE(nativeapps::catalog::availableAt(items, 4, 0), nullptr);
  EXPECT_STREQ(nativeapps::catalog::availableAt(items, 4, 0)->id, "memory");
  EXPECT_STREQ(nativeapps::catalog::availableAt(items, 4, 1)->id, "notes");
  EXPECT_EQ(nativeapps::catalog::availableAt(items, 4, 2), nullptr);
}
TEST(NativeAppsCatalog, AllUnavailableReturnsEmpty) {
  const Entry items[] = {{"a", launch, disabled}, {"b", launch, disabled}};
  EXPECT_EQ(nativeapps::catalog::availableCount(items, 2), 0u);
  EXPECT_EQ(nativeapps::catalog::availableAt(items, 2, 0), nullptr);
}
TEST(NativeAppsCatalog, RejectsOversizedOrNullCatalog) {
  std::array<Entry, nativeapps::catalog::kMaxApps + 1> items{};
  for (auto& item : items) item = {"valid", launch, nullptr};
  EXPECT_FALSE(nativeapps::catalog::valid(items.data(), items.size()));
  EXPECT_FALSE(nativeapps::catalog::valid(static_cast<const Entry*>(nullptr), 1));
  EXPECT_EQ(nativeapps::catalog::availableCount(items.data(), items.size()), 0u);
}
TEST(NativeAppsCatalog, ExactlyMaximumAllowedWhenIdsAreUnique) {
  constexpr Entry items[] = {{"app-0", launch, nullptr}, {"app-1", launch, nullptr},
                             {"app-2", launch, nullptr}, {"app-3", launch, nullptr},
                             {"app-4", launch, nullptr}, {"app-5", launch, nullptr},
                             {"app-6", launch, nullptr}, {"app-7", launch, nullptr},
                             {"app-8", launch, nullptr}, {"app-9", launch, nullptr},
                             {"app-10", launch, nullptr}, {"app-11", launch, nullptr}};
  static_assert(nativeapps::catalog::valid(items, 12));
  EXPECT_EQ(nativeapps::catalog::availableCount(items, 12), 12u);
}
TEST(NativeAppsCatalog, IdValidationIsNotAMemoryAllocationOrFsPath) {
  static_assert(nativeapps::catalog::validId("memory-status"));
  static_assert(!nativeapps::catalog::validId("../plugins"));
  EXPECT_FALSE(nativeapps::catalog::sameId("memory", "memory2"));
  EXPECT_TRUE(nativeapps::catalog::sameId("memory", "memory"));
}


TEST(NativeAppsCatalog, ContentAndStatePathsAreSeparate) {
  char path[64];
  ASSERT_TRUE(nativeapps::rootPath("memory-status", nativeapps::Directory::Content, path, sizeof(path)));
  EXPECT_STREQ(path, "/apps/memory-status");
  ASSERT_TRUE(nativeapps::rootPath("memory-status", nativeapps::Directory::State, path, sizeof(path)));
  EXPECT_STREQ(path, "/.crosspoint/apps/memory-status");
}
TEST(NativeAppsCatalog, RejectsPathTraversalWithoutWritingUserPath) {
  char path[64] = "preexisting";
  EXPECT_FALSE(nativeapps::rootPath("../plugins", nativeapps::Directory::Content, path, sizeof(path)));
  EXPECT_STREQ(path, "");
  EXPECT_FALSE(nativeapps::rootPath("memory-status", nativeapps::Directory::State, path, 5));
  EXPECT_STREQ(path, "");
  EXPECT_FALSE(nativeapps::rootPath("memory-status", nativeapps::Directory::State, nullptr, 64));
}
