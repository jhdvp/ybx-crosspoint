#include <gtest/gtest.h>

#include "util/YbxNativeAuthPolicy.h"

TEST(YbxNativeAuthPolicy, FirstPartyQRCodeOnly) {
  EXPECT_TRUE(ybxnative::trustedVerifyUrl("https://my.ybx.app/reader/pair?code=ABC-123"));
  EXPECT_TRUE(ybxnative::trustedVerifyUrl("https://ybx.app/activate"));
  EXPECT_FALSE(ybxnative::trustedVerifyUrl("http://my.ybx.app/reader/pair"));
  EXPECT_FALSE(ybxnative::trustedVerifyUrl("https://my.ybx.app.evil.com/reader/pair"));
  EXPECT_FALSE(ybxnative::trustedVerifyUrl("https://my.ybx.app@evil.com/reader/pair"));
  EXPECT_FALSE(ybxnative::trustedVerifyUrl("https://my.ybx.app/evil\nInjected"));
  EXPECT_FALSE(ybxnative::trustedVerifyUrl("https://my.ybx.app/" + std::string(600, 'a')));
}

TEST(YbxNativeAuthPolicy, RejectsMalformedDeviceChallenge) {
  constexpr auto goodUrl = "https://my.ybx.app/reader/pair?code=ABCDE";
  EXPECT_TRUE(ybxnative::validDeviceChallenge("ABCDE", "opaque-device-code", goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge("", "opaque-device-code", goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge("ABCDE", "", goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge(std::string(65, 'A'), "opaque-device-code", goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge("ABCDE", std::string(1025, 'a'), goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge("ABC\nDEF", "opaque-device-code", goodUrl));
  EXPECT_FALSE(ybxnative::validDeviceChallenge("ABCDE", "opaque-device-code",
                                                "https://my.ybx.app.evil.example/pair"));
}

TEST(YbxNativeAuthPolicy, BoundsUntrustedServerTiming) {
  EXPECT_EQ(ybxnative::pollSeconds(0), 3u);
  EXPECT_EQ(ybxnative::pollSeconds(5), 5u);
  EXPECT_EQ(ybxnative::pollSeconds(999999999), 60u);
  EXPECT_EQ(ybxnative::expiresSeconds(-12), 60u);
  EXPECT_EQ(ybxnative::expiresSeconds(900), 900u);
  EXPECT_EQ(ybxnative::expiresSeconds(999999999), 1800u);
}

TEST(YbxNativeAuthPolicy, ExplicitTerminalVersusPendingErrors) {
  EXPECT_TRUE(ybxnative::pendingGrantError("authorization_pending"));
  EXPECT_TRUE(ybxnative::pendingGrantError("slow_down"));
  EXPECT_TRUE(ybxnative::terminalGrantError("access_denied"));
  EXPECT_TRUE(ybxnative::terminalGrantError("expired_token"));
  EXPECT_FALSE(ybxnative::pendingGrantError("invalid_grant"));
}
