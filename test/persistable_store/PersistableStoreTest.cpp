#include <gtest/gtest.h>

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>

#include <string>

// Regression tests for the atomic settings/state persistence swap in
// PersistableStoreBase::writeDocToFile: every save goes temp-file ->
// replaceFile(), so a failure at any point leaves the previously persisted
// document intact instead of a truncated one that fails to parse on next boot.

namespace {

constexpr const char* STORE_PATH = "/.crosspoint/test.json";
constexpr const char* STORE_TMP = "/.crosspoint/test.json.tmp";

// A minimal concrete store: one int field, JSON round trip, and a flag that
// makes fromJson() request the legacy-migration resave like real stores do.
class TestStore : public PersistableStore<TestStore> {
  friend class PersistableStore<TestStore>;

  TestStore() = default;

 public:
  static const char* getFilePath() { return STORE_PATH; }

  void toJson(JsonDocument& doc) const { doc["value"] = value; }

  bool fromJson(JsonVariantConst doc) {
    value = doc["value"] | 0;
    if (migrateNextLoad) {
      migrateNextLoad = false;
      requestResave();
    }
    return true;
  }

  int value = 0;
  bool migrateNextLoad = false;
};

// Path beyond the temp-path buffer: these saves must still succeed through the
// legacy direct-write fallback.
class LongPathStore : public PersistableStore<LongPathStore> {
  friend class PersistableStore<LongPathStore>;

  LongPathStore() = default;

 public:
  static const char* getFilePath() {
    return "/.crosspoint/bookmarks/"
           "a_very_long_book_name_that_overflows_the_eighty_byte_temp_path_buffer.json";
  }

  void toJson(JsonDocument& doc) const { doc["ok"] = 1; }
  bool fromJson(JsonVariantConst) { return true; }
};

class PersistableStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakefs::reset();
    TestStore::getInstance().value = 0;
    TestStore::getInstance().migrateNextLoad = false;
  }
};

TEST_F(PersistableStoreTest, SaveSwapsTempOverDestination) {
  TestStore::getInstance().value = 7;

  ASSERT_TRUE(TestStore::getInstance().saveToFile());

  // Atomic path only: one temp write, one swap, no write to the destination.
  EXPECT_EQ(fakefs::writes, 1u);
  EXPECT_EQ(fakefs::swaps, 1u);
  EXPECT_TRUE(fakefs::files.count(STORE_PATH) != 0);
  EXPECT_NE(fakefs::fileContents(STORE_PATH).find("\"value\":7"), std::string::npos)
      << fakefs::fileContents(STORE_PATH);
  // The temp file must not survive the swap.
  EXPECT_FALSE(fakefs::files.count(STORE_TMP));
}

TEST_F(PersistableStoreTest, SwapFailureFallsBackToDirectWrite) {
  fakefs::failSwap = 0;  // replaceFile fails on the first call.
  TestStore::getInstance().value = 9;

  EXPECT_TRUE(TestStore::getInstance().saveToFile());

  // Two writes: the temp attempt plus the direct fallback.
  EXPECT_EQ(fakefs::writes, 2u);
  EXPECT_NE(fakefs::fileContents(STORE_PATH).find("\"value\":9"), std::string::npos);
  EXPECT_FALSE(fakefs::files.count(STORE_TMP)) << "failed temp file must be cleaned up";
}

TEST_F(PersistableStoreTest, TotalWriteFailureLeavesPreviousFileIntact) {
  fakefs::setFile(STORE_PATH, "{\"value\":1}");
  // Every write fails, modeling a dead or removed card mid-save.
  fakefs::failAllWrites = true;

  TestStore::getInstance().value = 2;
  EXPECT_FALSE(TestStore::getInstance().saveToFile());

  // The core regression: the previously persisted document is untouched.
  EXPECT_EQ(fakefs::fileContents(STORE_PATH), "{\"value\":1}");
  EXPECT_TRUE(TestStore::getInstance().loadFromFile());
  EXPECT_EQ(TestStore::getInstance().value, 1);
}

TEST_F(PersistableStoreTest, LeftoverTempFromCrashIsHarmless) {
  fakefs::setFile(STORE_PATH, "{\"value\":2}");
  // A crash mid-save left a partial temp file behind.
  fakefs::setFile(STORE_TMP, "{\"val");

  ASSERT_TRUE(TestStore::getInstance().loadFromFile());
  EXPECT_EQ(TestStore::getInstance().value, 2);

  // The next save overwrites the stale temp and swaps atomically.
  TestStore::getInstance().value = 3;
  ASSERT_TRUE(TestStore::getInstance().saveToFile());
  EXPECT_EQ(fakefs::swaps, 1u);
  EXPECT_NE(fakefs::fileContents(STORE_PATH).find("\"value\":3"), std::string::npos);
  EXPECT_FALSE(fakefs::files.count(STORE_TMP));
}

TEST_F(PersistableStoreTest, RoundTripKeepsLatestValue) {
  TestStore::getInstance().value = 1;
  ASSERT_TRUE(TestStore::getInstance().saveToFile());
  TestStore::getInstance().value = 2;
  ASSERT_TRUE(TestStore::getInstance().saveToFile());

  TestStore::getInstance().value = 0;
  ASSERT_TRUE(TestStore::getInstance().loadFromFile());
  EXPECT_EQ(TestStore::getInstance().value, 2);
}

TEST_F(PersistableStoreTest, MissingFileIsFirstBoot) {
  // No file on disk: load reports false (defaults stay), same as before.
  EXPECT_FALSE(TestStore::getInstance().loadFromFile());
  EXPECT_EQ(TestStore::getInstance().value, 0);
}

TEST_F(PersistableStoreTest, CorruptJsonFailsLoadAndKeepsDefaults) {
  fakefs::setFile(STORE_PATH, "{\"val");
  EXPECT_FALSE(TestStore::getInstance().loadFromFile());
  EXPECT_EQ(TestStore::getInstance().value, 0);
}

TEST_F(PersistableStoreTest, MigrationResaveUsesAtomicSwap) {
  fakefs::setFile(STORE_PATH, "{\"value\":5}");
  TestStore::getInstance().migrateNextLoad = true;

  ASSERT_TRUE(TestStore::getInstance().loadFromFile());

  // fromJson()'s requestResave() write must also go through the swap.
  EXPECT_EQ(fakefs::swaps, 1u);
  EXPECT_NE(fakefs::fileContents(STORE_PATH).find("\"value\":5"), std::string::npos);
  EXPECT_FALSE(fakefs::files.count(STORE_TMP));
}

TEST_F(PersistableStoreTest, OverlongPathFallsBackToDirectWrite) {
  ASSERT_TRUE(LongPathStore::getInstance().saveToFile());

  EXPECT_EQ(fakefs::swaps, 0u);
  EXPECT_EQ(fakefs::writes, 1u);
  EXPECT_NE(fakefs::fileContents(LongPathStore::getFilePath()).find("\"ok\":1"), std::string::npos);
}

}  // namespace
