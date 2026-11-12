#include <gtest/gtest.h>

#include "util/YbxServerMirror.h"

namespace m = ybxmirror;

namespace {

std::string hostAt(const std::string& url, uint8_t index) {
  // Plan needs millis() and storage, so the pure policy is exercised directly.
  const m::Url parsed = m::parseUrl(url);
  EXPECT_TRUE(parsed.mirrorable);
  const int family = m::familyForHost(parsed.host);
  EXPECT_GE(family, 0);
  uint8_t slots[m::SLOT_COUNT];
  const uint8_t n = m::candidates(m::Mode::Auto, m::Preference{}, 0, slots);
  EXPECT_GT(n, index);
  return m::withHost(url, parsed, m::FAMILY_HOSTS[family][slots[index]]);
}

}  // namespace

TEST(YbxServerMirror, FamiliesCoverMainEuPt) {
  ASSERT_EQ(m::FAMILY_COUNT, 3u);
  EXPECT_EQ(m::familyForHost("my.ybx.app"), 0);
  EXPECT_EQ(m::familyForHost("emy.ybx.app"), 0);
  EXPECT_EQ(m::familyForHost("pmy.ybx.app"), 0);
  EXPECT_EQ(m::familyForHost("opds.ybx.app"), 1);
  EXPECT_EQ(m::familyForHost("eopds.ybx.app"), 1);
  EXPECT_EQ(m::familyForHost("popds.ybx.app"), 1);
  EXPECT_EQ(m::familyForHost("sync.ybx.app"), 2);
  EXPECT_EQ(m::familyForHost("esync.ybx.app"), 2);
  EXPECT_EQ(m::familyForHost("psync.ybx.app"), 2);
  EXPECT_EQ(m::familyForHost("example.com"), -1);
  EXPECT_EQ(m::familyForHost("my.ybx.app.evil.test"), -1);
  EXPECT_EQ(m::familyForHost(""), -1);
}

TEST(YbxServerMirror, SlotLookup) {
  EXPECT_EQ(m::slotForHost(0, "my.ybx.app"), m::SLOT_MAIN);
  EXPECT_EQ(m::slotForHost(0, "emy.ybx.app"), m::SLOT_EU);
  EXPECT_EQ(m::slotForHost(0, "pmy.ybx.app"), m::SLOT_PT);
  EXPECT_EQ(m::slotForHost(0, "opds.ybx.app"), m::SLOT_COUNT);
  EXPECT_EQ(m::slotForHost(-1, "my.ybx.app"), m::SLOT_COUNT);
}

TEST(YbxServerMirror, ParseAcceptsOnlyPlainYbxHttpUrls) {
  EXPECT_TRUE(m::parseUrl("https://my.ybx.app/api/v1/crosspoint/catalog/lists").mirrorable);
  EXPECT_TRUE(m::parseUrl("http://sync.ybx.app/api/v1/crosspoint/kosync").mirrorable);
  EXPECT_TRUE(m::parseUrl("https://emy.ybx.app").mirrorable);
  EXPECT_TRUE(m::parseUrl("https://my.ybx.app/a?b=c#d").mirrorable);
  // Not a YBX host, not http(s), or carrying userinfo/a port: pass through.
  EXPECT_FALSE(m::parseUrl("https://example.com/x").mirrorable);
  EXPECT_FALSE(m::parseUrl("ftp://my.ybx.app/x").mirrorable);
  EXPECT_FALSE(m::parseUrl("https://user:pass@my.ybx.app/x").mirrorable);
  EXPECT_FALSE(m::parseUrl("https://my.ybx.app:8443/x").mirrorable);
  EXPECT_FALSE(m::parseUrl("/relative/path").mirrorable);
  EXPECT_FALSE(m::parseUrl("").mirrorable);
  EXPECT_FALSE(m::parseUrl("https://").mirrorable);
}

TEST(YbxServerMirror, WithHostKeepsSchemePathAndQuery) {
  const std::string url = "https://my.ybx.app/api/v1/books/7/download?v=1#frag";
  const m::Url parsed = m::parseUrl(url);
  ASSERT_TRUE(parsed.mirrorable);
  EXPECT_EQ(m::withHost(url, parsed, "pmy.ybx.app"), "https://pmy.ybx.app/api/v1/books/7/download?v=1#frag");
  EXPECT_EQ(m::withHost(url, parsed, "my.ybx.app"), url);
  EXPECT_EQ(hostAt("https://my.ybx.app/x", 0), "https://my.ybx.app/x");
  EXPECT_EQ(hostAt("https://my.ybx.app/x", 1), "https://emy.ybx.app/x");
  EXPECT_EQ(hostAt("https://my.ybx.app/x", 2), "https://pmy.ybx.app/x");
  // A mirror address in the config is normalised onto the same probe order.
  EXPECT_EQ(hostAt("https://pmy.ybx.app/x", 0), "https://my.ybx.app/x");
}

TEST(YbxServerMirror, ManualModesPinExactlyOneHost) {
  uint8_t slots[m::SLOT_COUNT];
  EXPECT_EQ(m::candidates(m::Mode::Main, m::Preference{}, 0, slots), 1);
  EXPECT_EQ(slots[0], m::SLOT_MAIN);
  EXPECT_EQ(m::candidates(m::Mode::Eu, m::Preference{}, 0, slots), 1);
  EXPECT_EQ(slots[0], m::SLOT_EU);
  EXPECT_EQ(m::candidates(m::Mode::Pt, m::Preference{}, 0, slots), 1);
  EXPECT_EQ(slots[0], m::SLOT_PT);
  // A remembered mirror never overrides a manual choice.
  m::Preference pref;
  pref.slot = m::SLOT_PT;
  pref.retryAt = 1000000;
  EXPECT_EQ(m::candidates(m::Mode::Main, pref, 0, slots), 1);
  EXPECT_EQ(slots[0], m::SLOT_MAIN);
}

TEST(YbxServerMirror, AutoProbesMainEuPt) {
  uint8_t slots[m::SLOT_COUNT];
  ASSERT_EQ(m::candidates(m::Mode::Auto, m::Preference{}, 0, slots), 3);
  EXPECT_EQ(slots[0], m::SLOT_MAIN);
  EXPECT_EQ(slots[1], m::SLOT_EU);
  EXPECT_EQ(slots[2], m::SLOT_PT);
}

TEST(YbxServerMirror, AutoRemembersTheHostThatAnswered) {
  uint8_t slots[m::SLOT_COUNT];
  m::Preference pref = m::afterSuccess(m::SLOT_EU, m::Preference{}, 1000);
  EXPECT_EQ(pref.slot, m::SLOT_EU);
  ASSERT_EQ(m::candidates(m::Mode::Auto, pref, 2000, slots), 3);
  EXPECT_EQ(slots[0], m::SLOT_EU);
  EXPECT_EQ(slots[1], m::SLOT_MAIN);
  EXPECT_EQ(slots[2], m::SLOT_PT);
  // Once the window closes, main is probed first again.
  ASSERT_EQ(m::candidates(m::Mode::Auto, pref, 1000 + m::RETRY_WINDOW_MS, slots), 3);
  EXPECT_EQ(slots[0], m::SLOT_MAIN);
}

TEST(YbxServerMirror, SuccessAtMainClearsTheWindow) {
  m::Preference pref = m::afterSuccess(m::SLOT_PT, m::Preference{}, 1000);
  EXPECT_NE(pref.retryAt, 0u);
  pref = m::afterSuccess(m::SLOT_MAIN, pref, 2000);
  EXPECT_EQ(pref.slot, m::SLOT_MAIN);
  EXPECT_EQ(pref.retryAt, 0u);
  EXPECT_EQ(m::firstSlot(pref, 3000), m::SLOT_MAIN);
}

TEST(YbxServerMirror, MirrorSuccessKeepsTheLaterWindow) {
  const m::Preference first = m::afterSuccess(m::SLOT_EU, m::Preference{}, 1000);
  const m::Preference again = m::afterSuccess(m::SLOT_EU, first, 2000);
  // A run of mirror successes must not shorten the throttle.
  EXPECT_EQ(again.retryAt, first.retryAt);
  const m::Preference later = m::afterSuccess(m::SLOT_PT, first, first.retryAt + 10);
  EXPECT_EQ(later.retryAt, first.retryAt + 10 + m::RETRY_WINDOW_MS);
}

TEST(YbxServerMirror, AllFailedThrottlesAwayFromMain) {
  const m::Preference pref = m::afterAllFailed(m::SLOT_MAIN, 5000);
  EXPECT_EQ(pref.slot, m::SLOT_EU);
  EXPECT_EQ(pref.retryAt, 5000 + m::RETRY_WINDOW_MS);
  uint8_t slots[m::SLOT_COUNT];
  ASSERT_EQ(m::candidates(m::Mode::Auto, pref, 6000, slots), 3);
  EXPECT_EQ(slots[0], m::SLOT_EU);
  // Already on a mirror: keep it rather than falling back to main.
  EXPECT_EQ(m::afterAllFailed(m::SLOT_PT, 5000).slot, m::SLOT_PT);
}

TEST(YbxServerMirror, DeadlineComparisonSurvivesMillisWraparound) {
  const uint32_t nearWrap = 0xFFFFFFF0u;
  EXPECT_TRUE(m::before(nearWrap, nearWrap + 100));
  EXPECT_FALSE(m::before(nearWrap + 100, nearWrap));
  EXPECT_FALSE(m::before(nearWrap, nearWrap));
  m::Preference pref = m::afterSuccess(m::SLOT_EU, m::Preference{}, nearWrap);
  EXPECT_EQ(m::firstSlot(pref, nearWrap + 100), m::SLOT_EU);
  EXPECT_EQ(m::firstSlot(pref, nearWrap + m::RETRY_WINDOW_MS), m::SLOT_MAIN);
}