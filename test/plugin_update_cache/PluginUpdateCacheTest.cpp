#include <HalStorage.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "util/PluginUpdateCache.h"

namespace {

using pluginupdates::addCatalogEntry;
using pluginupdates::boundedText;
using pluginupdates::carryNotified;
using pluginupdates::Entry;
using pluginupdates::isSafePluginName;
using pluginupdates::markNotified;
using pluginupdates::pendingIndex;
using pluginupdates::removeEntry;
using pluginupdates::shouldCheck;
using pluginupdates::UpdateSet;

const std::string INSTALLED_1_0_0 = "1.0.0";

TEST(PluginUpdateCache, OffersNewerCatalogVersion) {
  UpdateSet set;
  EXPECT_TRUE(addCatalogEntry(set, "reader", "Reader", "1.1.0", &INSTALLED_1_0_0));
  ASSERT_EQ(set.count, 1u);
  ASSERT_EQ(set.entries.size(), 1u);
  EXPECT_EQ(set.entries[0].name, "reader");
  EXPECT_EQ(set.entries[0].title, "Reader");
  EXPECT_EQ(set.entries[0].version, "1.1.0");
  EXPECT_FALSE(set.entries[0].notified);
}

TEST(PluginUpdateCache, IgnoresSameOlderAndMalformedVersions) {
  UpdateSet set;
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "1.0.0", &INSTALLED_1_0_0));
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "0.9.9", &INSTALLED_1_0_0));
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "1.1", &INSTALLED_1_0_0));
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "v1.1.0", &INSTALLED_1_0_0));
  EXPECT_EQ(set.count, 0u);
  EXPECT_TRUE(set.entries.empty());
}

TEST(PluginUpdateCache, InvalidInstalledVersionIsUpdatedButMissingPluginIsNot) {
  UpdateSet set;
  const std::string invalid = "not-a-version";
  EXPECT_TRUE(addCatalogEntry(set, "reader", "Reader", "1.1.0", &invalid));
  EXPECT_FALSE(addCatalogEntry(set, "gone", "Gone", "1.1.0", nullptr));
  EXPECT_EQ(set.count, 1u);
  ASSERT_EQ(set.entries.size(), 1u);
  EXPECT_EQ(set.entries[0].name, "reader");
}

TEST(PluginUpdateCache, RejectsNamesThatCannotBePluginFolders) {
  const std::string version = "1.0.0";
  const std::vector<std::string> names = {"", ".", "..", "a/b", "a\\b", "ctrl\nname", std::string(40, 'a')};
  for (const auto& name : names) {
    UpdateSet set;
    EXPECT_FALSE(addCatalogEntry(set, name, "Title", "1.1.0", &version)) << name;
  }
  UpdateSet set;
  EXPECT_TRUE(addCatalogEntry(set, "ok-name_1", "Title", "1.1.0", &version));
}

TEST(PluginUpdateCache, CountIncludesEntriesPastTheStorageCap) {
  UpdateSet set;
  const std::string version = "1.0.0";
  for (size_t i = 0; i < pluginupdates::MAX_ENTRIES + 3; i++) {
    EXPECT_TRUE(addCatalogEntry(set, "plugin" + std::to_string(i), "Plugin", "1.1.0", &version));
  }
  EXPECT_EQ(set.count, pluginupdates::MAX_ENTRIES + 3);
  EXPECT_EQ(set.entries.size(), pluginupdates::MAX_ENTRIES);
}

TEST(PluginUpdateCache, DuplicateCatalogRowsCountOnce) {
  UpdateSet set;
  const std::string version = "1.0.0";
  EXPECT_TRUE(addCatalogEntry(set, "reader", "Reader", "1.1.0", &version));
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "1.1.0", &version));
  EXPECT_FALSE(addCatalogEntry(set, "reader", "Reader", "1.2.0", &version));
  EXPECT_EQ(set.count, 1u);
  ASSERT_EQ(set.entries.size(), 1u);
  EXPECT_EQ(set.entries[0].version, "1.1.0");
}

TEST(PluginUpdateCache, InstalledVersionOfFindsScanRecords) {
  const pluginupdates::InstalledMap installed = {{"a", "1.0.0"}, {"b", ""}};
  ASSERT_NE(pluginupdates::installedVersionOf(installed, "a"), nullptr);
  EXPECT_EQ(*pluginupdates::installedVersionOf(installed, "a"), "1.0.0");
  // A manifest without a readable version still counts as installed, so the
  // caller receives the empty string rather than nullptr.
  ASSERT_NE(pluginupdates::installedVersionOf(installed, "b"), nullptr);
  EXPECT_EQ(*pluginupdates::installedVersionOf(installed, "b"), "");
  EXPECT_EQ(pluginupdates::installedVersionOf(installed, "c"), nullptr);
}

TEST(PluginUpdateCache, NotifiedFlagSurvivesOnlyTheSameNameAndVersion) {
  UpdateSet previous;
  const std::string installed = "1.0.0";
  addCatalogEntry(previous, "reader", "Reader", "1.1.0", &installed);
  markNotified(previous, 0);

  UpdateSet same;
  addCatalogEntry(same, "reader", "Reader", "1.1.0", &installed);
  carryNotified(same, previous);
  ASSERT_EQ(same.entries.size(), 1u);
  EXPECT_TRUE(same.entries[0].notified);
  EXPECT_EQ(pendingIndex(same), -1);

  UpdateSet newer;
  addCatalogEntry(newer, "reader", "Reader", "1.2.0", &installed);
  carryNotified(newer, previous);
  ASSERT_EQ(newer.entries.size(), 1u);
  EXPECT_FALSE(newer.entries[0].notified);
  EXPECT_EQ(pendingIndex(newer), 0);

  UpdateSet renamed;
  addCatalogEntry(renamed, "reader2", "Reader", "1.1.0", &installed);
  carryNotified(renamed, previous);
  EXPECT_FALSE(renamed.entries[0].notified);
}

TEST(PluginUpdateCache, PendingEntryIsMarkedNotifiedOnce) {
  UpdateSet set;
  const std::string installed = "1.0.0";
  addCatalogEntry(set, "a", "A", "1.1.0", &installed);
  addCatalogEntry(set, "b", "B", "1.1.0", &installed);
  EXPECT_EQ(pendingIndex(set), 0);

  markNotified(set, 0);
  EXPECT_EQ(pendingIndex(set), 1);
  markNotified(set, 1);
  EXPECT_EQ(pendingIndex(set), -1);

  markNotified(set, 5);  // out of range: no-op, no crash
  EXPECT_EQ(pendingIndex(set), -1);
}

TEST(PluginUpdateCache, RemovingEntryDropsItAndTheCount) {
  UpdateSet set;
  const std::string installed = "1.0.0";
  addCatalogEntry(set, "a", "A", "1.1.0", &installed);
  addCatalogEntry(set, "b", "B", "1.1.0", &installed);
  ASSERT_TRUE(removeEntry(set, "a"));
  EXPECT_EQ(set.count, 1u);
  ASSERT_EQ(set.entries.size(), 1u);
  EXPECT_EQ(set.entries[0].name, "b");
  EXPECT_FALSE(removeEntry(set, "a"));
  EXPECT_EQ(set.count, 1u);
}

TEST(PluginUpdateCache, DailyThrottleWithoutTrustworthyClock) {
  constexpr int64_t day = pluginupdates::CHECK_INTERVAL_SECONDS;
  // No clock: only the caller's forced first scan runs (shouldCheck false).
  EXPECT_FALSE(shouldCheck(0, 0));
  EXPECT_FALSE(shouldCheck(1234, 0));
  // First valid-clock scan refreshes a cache stamped by an unscanned clock.
  EXPECT_TRUE(shouldCheck(0, day));
  // Within a day: no rescan.
  EXPECT_FALSE(shouldCheck(1000, 1000 + day - 1));
  // Exactly one day or more: rescan.
  EXPECT_TRUE(shouldCheck(1000, 1000 + day));
  EXPECT_TRUE(shouldCheck(1000, 1000 + day * 2));
  // A clock that moved backwards is not trusted; rescan and restamp.
  EXPECT_TRUE(shouldCheck(2000, 1000));
}

TEST(PluginUpdateCache, BoundedTextKeepsUtf8AndDropsControls) {
  EXPECT_EQ(boundedText("Plain", 64), "Plain");
  EXPECT_EQ(boundedText("a\nb\tc\x7f", 64), "abc");
  // "Ж" is two bytes; a 5-byte budget must not split it.
  const std::string cyrillic = "ЖЖЖ";
  EXPECT_EQ(boundedText(cyrillic, 5), "ЖЖ");
  EXPECT_EQ(boundedText(cyrillic, 6), cyrillic);
}

TEST(PluginUpdateCache, SafePluginNames) {
  EXPECT_TRUE(isSafePluginName("pluginhub"));
  EXPECT_TRUE(isSafePluginName("plugin_1.2"));
  EXPECT_FALSE(isSafePluginName(""));
  EXPECT_FALSE(isSafePluginName("."));
  EXPECT_FALSE(isSafePluginName(".."));
  EXPECT_FALSE(isSafePluginName("a/b"));
  EXPECT_FALSE(isSafePluginName("a\\b"));
  EXPECT_FALSE(isSafePluginName(std::string(pluginupdates::MAX_NAME_BYTES + 1, 'a')));
  EXPECT_TRUE(isSafePluginName(std::string(pluginupdates::MAX_NAME_BYTES, 'a')));
}

// The SD half: PluginUpdateCache.cpp against stubs/HalStorage.h, with installed
// plugins resolved through the folder table at the bottom of this file.
constexpr const char* TMP_PATH = "/.crosspoint/plugin-updates.json.tmp";
// PluginUpdateCache.cpp's private read cap for the cache file.
constexpr size_t MAX_CACHE_FILE_BYTES = 4096;

std::map<std::string, std::string>& pluginDirTable() {
  static std::map<std::string, std::string> dirs;
  return dirs;
}

pluginupdates::Entry makeEntry(const std::string& name, const std::string& version, const bool notified = false) {
  pluginupdates::Entry entry;
  entry.name = name;
  entry.title = name + " title";
  entry.version = version;
  entry.notified = notified;
  return entry;
}

pluginupdates::UpdateSet makeSet(const std::vector<std::pair<std::string, std::string>>& nameVersions,
                                 const uint16_t count) {
  pluginupdates::UpdateSet set;
  set.count = count;
  for (const auto& [name, version] : nameVersions) set.entries.push_back(makeEntry(name, version));
  return set;
}

class PluginUpdateCacheSd : public ::testing::Test {
 protected:
  void SetUp() override {
    fakefs::reset();
    pluginDirTable().clear();
    // The module keeps one RAM copy of the file; every test starts without it.
    pluginupdates::invalidate();
  }

  // An installed plugin folder carrying the given manifest.json.
  void install(const std::string& name, const std::string& manifestJson) {
    const std::string dir = "/.crosspoint/plugins/" + name;
    pluginDirTable()[name] = dir;
    fakefs::setFile(dir + "/manifest.json", manifestJson);
  }

  // Re-reads the cache file, bypassing the RAM copy.
  pluginupdates::UpdateSet reload(int64_t& checkedAt) {
    pluginupdates::invalidate();
    pluginupdates::UpdateSet set;
    EXPECT_TRUE(pluginupdates::load(set, checkedAt));
    return set;
  }
};

TEST_F(PluginUpdateCacheSd, SaveWritesThroughTheTempFile) {
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}, {"notes", "2.0.0"}}, 2), 5000));
  EXPECT_TRUE(fakefs::hasFile(pluginupdates::CACHE_PATH));
  EXPECT_FALSE(fakefs::hasFile(TMP_PATH));
  EXPECT_EQ(fakefs::swaps, 1u);
  ASSERT_FALSE(fakefs::directories.empty());
  EXPECT_EQ(fakefs::directories.front(), "/.crosspoint");

  int64_t checkedAt = 0;
  const auto reloaded = reload(checkedAt);
  EXPECT_EQ(checkedAt, 5000);
  EXPECT_EQ(reloaded.count, 2u);
  ASSERT_EQ(reloaded.entries.size(), 2u);
  EXPECT_EQ(reloaded.entries[0].name, "reader");
  EXPECT_EQ(reloaded.entries[0].title, "reader title");
  EXPECT_EQ(reloaded.entries[1].version, "2.0.0");
}

TEST_F(PluginUpdateCacheSd, NotifiedFlagRoundTrips) {
  pluginupdates::UpdateSet set = makeSet({{"reader", "1.1.0"}}, 1);
  set.entries[0].notified = true;
  ASSERT_TRUE(pluginupdates::save(set, 7000));
  int64_t checkedAt = 0;
  const auto reloaded = reload(checkedAt);
  ASSERT_EQ(reloaded.entries.size(), 1u);
  EXPECT_TRUE(reloaded.entries[0].notified);
}

TEST_F(PluginUpdateCacheSd, FailedSwapKeepsThePreviousCache) {
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}}, 1), 1000));
  const std::string before = fakefs::fileContents(pluginupdates::CACHE_PATH);
  fakefs::failSwap = 0;
  EXPECT_FALSE(pluginupdates::save(makeSet({{"notes", "2.0.0"}}, 1), 2000));
  EXPECT_EQ(fakefs::fileContents(pluginupdates::CACHE_PATH), before);
  EXPECT_FALSE(fakefs::hasFile(TMP_PATH));
  // The RAM copy stays in step with the file, which is still the old set.
  EXPECT_TRUE(pluginupdates::hasCachedUpdate("reader"));
  EXPECT_FALSE(pluginupdates::hasCachedUpdate("notes"));
}

TEST_F(PluginUpdateCacheSd, FailedWriteLeavesNoCacheFile) {
  fakefs::failWrite = 0;
  EXPECT_FALSE(pluginupdates::save(makeSet({{"reader", "1.1.0"}}, 1), 1000));
  EXPECT_FALSE(fakefs::hasFile(pluginupdates::CACHE_PATH));
  EXPECT_EQ(fakefs::swaps, 0u);
}

TEST_F(PluginUpdateCacheSd, LoadRejectsMissingCorruptAndOversizedFiles) {
  pluginupdates::UpdateSet set = makeSet({{"reader", "1.1.0"}}, 1);
  int64_t checkedAt = 123;
  EXPECT_FALSE(pluginupdates::load(set, checkedAt));
  EXPECT_TRUE(set.entries.empty());
  EXPECT_EQ(set.count, 0u);
  EXPECT_EQ(checkedAt, 0);

  fakefs::setFile(pluginupdates::CACHE_PATH, "{not json");
  EXPECT_FALSE(pluginupdates::load(set, checkedAt));

  // A file over the read cap is refused without being parsed.
  fakefs::setFile(pluginupdates::CACHE_PATH, std::string(MAX_CACHE_FILE_BYTES + 1, 'x'));
  EXPECT_FALSE(pluginupdates::load(set, checkedAt));
}

TEST_F(PluginUpdateCacheSd, LoadKeepsUsableRowsAndTheStoredCount) {
  fakefs::setFile(pluginupdates::CACHE_PATH, R"({"checked_at":9,"count":5,"items":[)"
                                             R"({"name":"a/b","title":"T","version":"1.1.0"},)"
                                             R"({"name":"empty","title":"E","version":""},)"
                                             R"({"name":"keep","title":"K","version":"1.2.0","notified":true}]})");
  pluginupdates::UpdateSet set;
  int64_t checkedAt = 0;
  ASSERT_TRUE(pluginupdates::load(set, checkedAt));
  EXPECT_EQ(checkedAt, 9);
  ASSERT_EQ(set.entries.size(), 1u);
  EXPECT_EQ(set.entries[0].name, "keep");
  EXPECT_TRUE(set.entries[0].notified);
  // The count covers plugins that did not fit in the stored rows.
  EXPECT_EQ(set.count, 5u);
}

TEST_F(PluginUpdateCacheSd, CachedReadsTheFileOnlyOnce) {
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}}, 1), 1000));
  // Forget the RAM copy the way a reboot does: the next caller reads the file.
  pluginupdates::invalidate();
  fakefs::reads = 0;
  // Home's badge, the picker row, the prompt and the daily stamp check all ask
  // the module; only the first one may touch SD.
  EXPECT_EQ(pluginupdates::cachedCount(), 1u);
  EXPECT_TRUE(pluginupdates::hasCachedUpdate("reader"));
  EXPECT_EQ(pluginupdates::cached().checkedAt, 1000);
  EXPECT_EQ(pluginupdates::cachedCount(), 1u);
  EXPECT_EQ(fakefs::reads, 1u);
}

TEST_F(PluginUpdateCacheSd, SaveSeedsTheRamCopy) {
  fakefs::reads = 0;
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}}, 3), 4000));
  EXPECT_EQ(pluginupdates::cachedCount(), 3u);
  EXPECT_EQ(pluginupdates::cached().checkedAt, 4000);
  EXPECT_EQ(fakefs::reads, 0u);
}

TEST_F(PluginUpdateCacheSd, InvalidateDropsTheRamCopy) {
  fakefs::setFile(pluginupdates::CACHE_PATH, R"({"checked_at":1,"count":1,"items":[]})");
  EXPECT_EQ(pluginupdates::cachedCount(), 1u);
  fakefs::setFile(pluginupdates::CACHE_PATH, R"({"checked_at":2,"count":3,"items":[]})");
  // Without a reason to doubt the file, the copy is reused.
  EXPECT_EQ(pluginupdates::cachedCount(), 1u);
  pluginupdates::invalidate();
  EXPECT_EQ(pluginupdates::cachedCount(), 3u);
  EXPECT_EQ(fakefs::reads, 2u);
}

TEST_F(PluginUpdateCacheSd, TakePromptMarksEachVersionNotifiedOnce) {
  install("reader", R"({"version":"1.0.0"})");
  install("notes", R"({"version":"2.0.0"})");
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}, {"notes", "2.1.0"}}, 2), 1000));

  pluginupdates::Entry entry;
  ASSERT_TRUE(pluginupdates::takePrompt(entry));
  EXPECT_EQ(entry.name, "reader");
  EXPECT_EQ(entry.title, "reader title");
  EXPECT_EQ(entry.version, "1.1.0");
  ASSERT_TRUE(pluginupdates::takePrompt(entry));
  EXPECT_EQ(entry.name, "notes");
  EXPECT_FALSE(pluginupdates::takePrompt(entry));

  int64_t checkedAt = 0;
  const auto reloaded = reload(checkedAt);
  EXPECT_EQ(checkedAt, 1000);
  ASSERT_EQ(reloaded.entries.size(), 2u);
  // One prompt per new version, across re-reads.
  EXPECT_TRUE(reloaded.entries[0].notified);
  EXPECT_TRUE(reloaded.entries[1].notified);
  // Two prompts, one file write each: nothing was persisted for the third call.
  EXPECT_EQ(fakefs::swaps, 3u);
}

TEST_F(PluginUpdateCacheSd, TakePromptDropsRowsThatNoLongerNeedAnUpdate) {
  install("reader", R"({"version":"1.1.0"})");  // already at the catalog version
  pluginupdates::UpdateSet set = makeSet({{"reader", "1.1.0"}, {"gone", "2.0.0"}}, 2);
  ASSERT_TRUE(pluginupdates::save(set, 1000));

  pluginupdates::Entry entry;
  EXPECT_FALSE(pluginupdates::takePrompt(entry));
  int64_t checkedAt = 0;
  const auto reloaded = reload(checkedAt);
  EXPECT_TRUE(reloaded.entries.empty());
  EXPECT_EQ(reloaded.count, 0u);
  EXPECT_EQ(pluginupdates::cachedCount(), 0u);
}

TEST_F(PluginUpdateCacheSd, ForgetDropsOnePluginAndItsCount) {
  ASSERT_TRUE(pluginupdates::save(makeSet({{"reader", "1.1.0"}, {"notes", "2.1.0"}}, 3), 1000));
  EXPECT_TRUE(pluginupdates::forget("reader"));
  EXPECT_FALSE(pluginupdates::forget("reader"));
  EXPECT_FALSE(pluginupdates::hasCachedUpdate("reader"));
  EXPECT_EQ(pluginupdates::cachedCount(), 2u);

  int64_t checkedAt = 0;
  const auto reloaded = reload(checkedAt);
  ASSERT_EQ(reloaded.entries.size(), 1u);
  EXPECT_EQ(reloaded.entries[0].name, "notes");
  EXPECT_EQ(reloaded.count, 2u);
}

TEST_F(PluginUpdateCacheSd, ReadsInstalledManifestVersion) {
  std::string version = "unset";
  install("reader", R"({"name":"Reader","version":"1.0.0"})");
  ASSERT_TRUE(pluginupdates::installedVersion("reader", version));
  EXPECT_EQ(version, "1.0.0");

  // Absent plugins and unreadable manifests give the caller no version, so no
  // update is offered for them.
  EXPECT_FALSE(pluginupdates::installedVersion("absent", version));
  install("broken", "{not json");
  EXPECT_FALSE(pluginupdates::installedVersion("broken", version));
  install("oversize", std::string(9000, 'x'));
  EXPECT_FALSE(pluginupdates::installedVersion("oversize", version));

  // A manifest that parses without a version still counts as installed.
  install("noversion", R"({"name":"No Version"})");
  ASSERT_TRUE(pluginupdates::installedVersion("noversion", version));
  EXPECT_TRUE(version.empty());

  // The scan map reads the same folders directly.
  version = "unset";
  ASSERT_TRUE(pluginupdates::readManifestVersion("/.crosspoint/plugins/reader", version));
  EXPECT_EQ(version, "1.0.0");
  EXPECT_FALSE(pluginupdates::readManifestVersion("/.crosspoint/plugins/none", version));
}

}  // namespace

namespace PluginLocations {
// The code under test resolves a plugin folder through the HAL. The host build
// has no SD roots, so this table stands in for the scan.
std::string findPluginDir(const char* name) {
  const auto found = pluginDirTable().find(name);
  return found == pluginDirTable().end() ? std::string() : found->second;
}
}  // namespace PluginLocations
