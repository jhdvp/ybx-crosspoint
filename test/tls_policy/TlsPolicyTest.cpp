#include <gtest/gtest.h>

#include "network/TlsPolicy.h"

using namespace tlspolicy;

namespace {
// 2025-01-01T00:00:00Z, the same floor TrustedTime uses.
constexpr int64_t kFloor = 1735689600LL;
}  // namespace

TEST(TlsPolicyClock, BelowTheFloorIsNotUsable) {
  EXPECT_FALSE(clockReady(0));
  EXPECT_FALSE(clockReady(kFloor - 1));
  EXPECT_TRUE(clockReady(kFloor));
  EXPECT_TRUE(clockReady(kFloor + 86400));
}

TEST(TlsPolicyClock, ReadyClockDoesNotSync) {
  int syncs = 0;
  const bool ok = ensureClock([] { return kFloor; },
                              [&syncs] {
                                syncs++;
                                return true;
                              });
  EXPECT_TRUE(ok);
  EXPECT_EQ(syncs, 0);
}

TEST(TlsPolicyClock, NotSetClockSyncsOnceThenProceeds) {
  int64_t epoch = 0;
  int syncs = 0;
  const bool ok = ensureClock([&epoch] { return epoch; },
                              [&] {
                                syncs++;
                                epoch = kFloor;  // SNTP answered
                                return true;
                              });
  EXPECT_TRUE(ok);
  EXPECT_EQ(syncs, 1);
}

TEST(TlsPolicyClock, FailedSyncRefusesTheRequest) {
  int syncs = 0;
  const bool ok = ensureClock([] { return 0; },
                              [&syncs] {
                                syncs++;
                                return false;  // no Wi-Fi / SNTP timeout
                              });
  EXPECT_FALSE(ok);
  EXPECT_EQ(syncs, 1);
}

TEST(TlsPolicyClock, SyncClaimingSuccessWithoutATimeStillRefuses) {
  // A sync that returns true but leaves the clock below the floor (e.g. an
  // RTC reading in the past) must not let certificate dates be "checked".
  const bool ok = ensureClock([] { return 0; }, [] { return true; });
  EXPECT_FALSE(ok);
}

TEST(TlsPolicyOrigin, NormalizesSchemeHostAndDefaultPort) {
  EXPECT_EQ(originOf("https://My.YBX.app/path?q=1#frag"), "https://my.ybx.app:443");
  EXPECT_EQ(originOf("http://example.com"), "http://example.com:80");
  EXPECT_EQ(originOf("HTTPS://Example.COM"), "https://example.com:443");
  EXPECT_EQ(originOf("https://example.com:8443/x"), "https://example.com:8443");
  EXPECT_EQ(originOf("https://example.com:443/x"), "https://example.com:443");
}

TEST(TlsPolicyOrigin, DropsUserInfo) {
  EXPECT_EQ(originOf("https://user:secret@example.com/x"), "https://example.com:443");
  EXPECT_EQ(originOf("https://user@example.com:8443/x"), "https://example.com:8443");
}

TEST(TlsPolicyOrigin, HandlesIpv6Literals) {
  EXPECT_EQ(originOf("https://[2001:DB8::1]/x"), "https://[2001:db8::1]:443");
  EXPECT_EQ(originOf("https://[2001:db8::1]:8443/x"), "https://[2001:db8::1]:8443");
}

TEST(TlsPolicyOrigin, RejectsStringsWithoutAScheme) {
  EXPECT_EQ(originOf(""), "");
  EXPECT_EQ(originOf("example.com/x"), "");
  EXPECT_EQ(originOf("/relative/path"), "");
}

TEST(TlsPolicyOrigin, RecognizesHttpsOnly) {
  EXPECT_TRUE(isHttpsUrl("https://ybx.app/x"));
  EXPECT_TRUE(isHttpsUrl("HTTPS://ybx.app/x"));
  EXPECT_FALSE(isHttpsUrl("http://ybx.app/x"));
  EXPECT_FALSE(isHttpsUrl(""));
}

TEST(TlsPolicyRedirect, SameOriginIsFollowedWithCredentials) {
  const RedirectDecision d = decideRedirect("https://ybx.app/a", "https://ybx.app/b");
  EXPECT_TRUE(d.follow);
  EXPECT_TRUE(d.sameOrigin);
  EXPECT_FALSE(d.downgrade);
}

TEST(TlsPolicyRedirect, DefaultPortIsTheSameOrigin) {
  const RedirectDecision d = decideRedirect("https://ybx.app/a", "https://ybx.app:443/b");
  EXPECT_TRUE(d.follow);
  EXPECT_TRUE(d.sameOrigin);
}

TEST(TlsPolicyRedirect, CrossOriginIsFollowedWithoutCredentials) {
  const RedirectDecision d = decideRedirect("https://api.github.com/repos", "https://release-assets.example/file");
  EXPECT_TRUE(d.follow);
  EXPECT_FALSE(d.sameOrigin);
  EXPECT_FALSE(d.downgrade);
}

TEST(TlsPolicyRedirect, UserInfoCannotFakeASameOrigin) {
  const RedirectDecision d = decideRedirect("https://ybx.app/a", "https://user:pass@ybx.app/b");
  EXPECT_TRUE(d.sameOrigin);
  // originOf() drops the userinfo, so the credential decision is made on the
  // real host, not on attacker-controlled text in the URL.
  EXPECT_EQ(originOf("https://user:pass@ybx.app/b"), "https://ybx.app:443");
}

TEST(TlsPolicyRedirect, HttpsToHttpIsRefused) {
  const RedirectDecision d = decideRedirect("https://ybx.app/a", "http://ybx.app/b");
  EXPECT_FALSE(d.follow);
  EXPECT_TRUE(d.downgrade);
  EXPECT_FALSE(d.sameOrigin);
}

TEST(TlsPolicyRedirect, UppercaseSchemeCannotBypassTheDowngradeRule) {
  // The transport parses schemes case-insensitively, so the redirect policy
  // must too: "HTTPS://" is a secure start, not an unknown scheme.
  const RedirectDecision d = decideRedirect("HTTPS://ybx.app/a", "HTTP://ybx.app/b");
  EXPECT_FALSE(d.follow);
  EXPECT_TRUE(d.downgrade);
}

TEST(TlsPolicyRedirect, StepUpToHttpsIsFollowed) {
  const RedirectDecision d = decideRedirect("http://ybx.app/a", "https://ybx.app/b");
  EXPECT_TRUE(d.follow);
  EXPECT_FALSE(d.downgrade);
}

TEST(TlsPolicyRedirect, ExplicitDowngradeOptInOnlyForLegacyCallers) {
  const RedirectDecision d = decideRedirect("https://ybx.app/a", "http://ybx.app/b", true);
  EXPECT_TRUE(d.follow);
  EXPECT_TRUE(d.downgrade);
}

TEST(TlsPolicyRedirect, UnparseableLocationStops) {
  EXPECT_FALSE(decideRedirect("https://ybx.app/a", "").follow);
  EXPECT_FALSE(decideRedirect("", "https://ybx.app/a").follow);
}

TEST(TlsPolicyFailure, OnlyTrustFailuresNeedTheTlsExplanation) {
  EXPECT_TRUE(isTrustFailure(Failure::Clock));
  EXPECT_TRUE(isTrustFailure(Failure::Certificate));
  EXPECT_TRUE(isTrustFailure(Failure::Hostname));
  EXPECT_TRUE(isTrustFailure(Failure::InsecureRedirect));
  EXPECT_TRUE(isTrustFailure(Failure::Configuration));
  EXPECT_FALSE(isTrustFailure(Failure::None));
  EXPECT_FALSE(isTrustFailure(Failure::Network));
}
