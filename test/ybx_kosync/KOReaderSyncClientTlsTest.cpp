#include <gtest/gtest.h>

#include <SecureHttpClient.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "network/TlsTransport.h"

bool PersistableStoreBase::writeDocToFile(const char*, const ArduinoJson::JsonDocument&) { return true; }
std::string PersistableStoreBase::extractPassword(ArduinoJson::JsonVariantConst, bool&, size_t, bool&) { return ""; }
std::string PersistableStoreBase::extractPassword(ArduinoJson::JsonVariantConst, bool&) { return ""; }

class KOReaderSyncClientTlsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::insecureCalled = false;
    fake::trustedClient = false;
    fake::tlsPrepared = false;
    fake::allowVerifiedTls = true;
    fake::tlsAttempts = 0;
    KOREADER_STORE.setCredentials("testuser", "testpass");
  }
};

TEST_F(KOReaderSyncClientTlsTest, HttpsRequiresVerifiedTlsAndNeverCallsInsecure) {
  KOREADER_STORE.setServerUrl("https://kosync.example.com");
  KOReaderSyncClient client;
  client.authenticate();
  EXPECT_FALSE(fake::insecureCalled);
  EXPECT_TRUE(fake::trustedClient);
}

TEST_F(KOReaderSyncClientTlsTest, HttpsFailsWhenTlsVerificationFails) {
  KOREADER_STORE.setServerUrl("https://kosync.example.com");
  fake::allowVerifiedTls = false;
  KOReaderSyncClient client;
  EXPECT_EQ(client.authenticate(), KOReaderSyncClient::NETWORK_ERROR);
  EXPECT_FALSE(fake::insecureCalled);
}

TEST_F(KOReaderSyncClientTlsTest, PlainHttpDoesNotCallInsecureOrTls) {
  KOREADER_STORE.setServerUrl("http://192.168.1.100:8080");
  KOReaderSyncClient client;
  client.authenticate();
  EXPECT_FALSE(fake::insecureCalled);
  EXPECT_FALSE(fake::tlsPrepared);
}
