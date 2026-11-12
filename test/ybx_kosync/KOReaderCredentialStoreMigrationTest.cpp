#include <gtest/gtest.h>

#include "KOReaderCredentialStore.h"

// The credential store's JSON is built by loadFromFile() on the device; here
// fromJson() is driven directly, and the storage side of a resave is stubbed.
bool PersistableStoreBase::writeDocToFile(const char*, const ArduinoJson::JsonDocument&) { return true; }
std::string PersistableStoreBase::extractPassword(ArduinoJson::JsonVariantConst, bool&, size_t, bool&) { return ""; }
std::string PersistableStoreBase::extractPassword(ArduinoJson::JsonVariantConst, bool&) { return ""; }

namespace {

constexpr const char* CROSSPOINT_URL = "https://sync.crosspointreader.com";

bool load(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  return KOREADER_STORE.fromJson(doc.as<JsonVariantConst>());
}

// A v3 config (the version that introduced serverType) stored the profile the
// human picked. Raising CONFIG_VERSION for an unrelated default must not
// re-derive that field from the URL: the built-in URL alone says CROSSPOINT, so
// an explicit choice would be silently overwritten.
TEST(KOReaderConfigMigration, KeptServerTypeSurvivesTheV4DefaultChange) {
  ASSERT_TRUE(load(R"({"cfgVersion":3,"username":"u","serverUrl":"https://sync.crosspointreader.com",
                      "serverType":1,"matchMethod":1,"syncBehavior":1})"));
  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::KOSYNC);
  EXPECT_STREQ(KOREADER_STORE.getServerUrl().c_str(), CROSSPOINT_URL);

  ASSERT_TRUE(load(R"({"cfgVersion":3,"username":"u","serverUrl":"https://sync.example.com",
                      "serverType":2,"matchMethod":0,"syncBehavior":0})"));
  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::OTHER);
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::FILENAME);
}

// Pre-v3 configs have no profile key at all: those still get the URL-derived
// value, so upgrading from them does not switch protocol semantics silently.
TEST(KOReaderConfigMigration, ServerTypeIsDerivedOnlyWhenTheConfigHasNoProfile) {
  ASSERT_TRUE(load(R"({"cfgVersion":2,"username":"u","serverUrl":"https://sync.crosspointreader.com"})"));
  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::CROSSPOINT);

  ASSERT_TRUE(load(R"({"cfgVersion":2,"username":"u","serverUrl":"https://sync.example.com"})"));
  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::KOSYNC);

  // A malformed v3/v4 file that lost the key falls back to the same rule.
  ASSERT_TRUE(load(R"({"cfgVersion":4,"username":"u","serverUrl":"https://sync.crosspointreader.com"})"));
  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::CROSSPOINT);
}

// The v4 default is the content hash; a config written before v4 that never
// stored a method was matching by filename, so that is what gets pinned.
TEST(KOReaderConfigMigration, PreV4MatchMethodIsPinnedToFilenames) {
  ASSERT_TRUE(load(R"({"cfgVersion":3,"username":"u","serverUrl":"https://sync.example.com","serverType":1})"));
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::FILENAME);

  ASSERT_TRUE(load(R"({"cfgVersion":4,"username":"u","serverUrl":"https://sync.example.com","serverType":1})"));
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::BINARY);
}

// What the firmware writes must be what it reads back, unchanged.
TEST(KOReaderConfigMigration, RoundTripKeepsEveryStoredChoice) {
  KOREADER_STORE.setCredentials("u", "p");
  KOREADER_STORE.setServerUrl(CROSSPOINT_URL);
  KOREADER_STORE.setServerType(KOReaderServerType::OTHER);
  KOREADER_STORE.setMatchMethod(DocumentMatchMethod::FILENAME);
  KOREADER_STORE.setSyncBehavior(KOReaderSyncBehavior::ASK_EVERY_TIME);

  JsonDocument doc;
  KOREADER_STORE.toJson(doc);
  ASSERT_TRUE(KOREADER_STORE.fromJson(doc.as<JsonVariantConst>()));

  EXPECT_EQ(KOREADER_STORE.getServerType(), KOReaderServerType::OTHER);
  EXPECT_EQ(KOREADER_STORE.getMatchMethod(), DocumentMatchMethod::FILENAME);
  EXPECT_EQ(KOREADER_STORE.getSyncBehavior(), KOReaderSyncBehavior::ASK_EVERY_TIME);

  // The saved file carries the version this firmware writes, so the next load
  // takes the "already migrated" path rather than deriving anything.
  JsonDocument again;
  KOREADER_STORE.toJson(again);
  EXPECT_EQ(again["cfgVersion"].as<uint8_t>(), 4);
}

}  // namespace
