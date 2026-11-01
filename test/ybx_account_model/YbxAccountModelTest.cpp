#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "util/YbxAccountModel.h"

using namespace ybxaccount;

TEST(YbxAccountModel, ClassifiesEveryProviderPathAndIgnoresQueries) {
  EXPECT_EQ(classifyListUrl("https://ybx.example/crosspoint/catalog/books/123.opds"), ListKind::Yandex);
  EXPECT_EQ(classifyListUrl("https://ybx.example/crosspoint/wb/feed?token=abc"), ListKind::Wb);
  EXPECT_EQ(classifyListUrl("https://ybx.example/crosspoint/litres/feed?token=abc"), ListKind::Litres);
  // The query is stripped first, so a "?/crosspoint/wb/" trick cannot misclassify.
  EXPECT_EQ(classifyListUrl("https://ybx.example/other/list?path=/crosspoint/wb/"), ListKind::None);
  EXPECT_EQ(classifyListUrl("https://ybx.example/crosspoint/wbcd/list"), ListKind::None);
  EXPECT_EQ(classifyListUrl(""), ListKind::None);
}

TEST(YbxAccountModel, MapsHttpStatusesToAccountOutcomes) {
  EXPECT_EQ(listsOutcomeFromStatus(200, false), ListsOutcome::Ok);
  EXPECT_EQ(listsOutcomeFromStatus(204, false), ListsOutcome::Ok);
  EXPECT_EQ(listsOutcomeFromStatus(401, false), ListsOutcome::InvalidToken);
  EXPECT_EQ(listsOutcomeFromStatus(403, false), ListsOutcome::InvalidToken);
  EXPECT_EQ(listsOutcomeFromStatus(401, true), ListsOutcome::DeviceRemoved);
  EXPECT_EQ(listsOutcomeFromStatus(403, true), ListsOutcome::DeviceRemoved);
  EXPECT_EQ(listsOutcomeFromStatus(409, false), ListsOutcome::ProviderSession);
  EXPECT_EQ(listsOutcomeFromStatus(-1, false), ListsOutcome::Offline);
  EXPECT_EQ(listsOutcomeFromStatus(500, false), ListsOutcome::Failed);
  EXPECT_EQ(listsOutcomeFromStatus(429, true), ListsOutcome::Failed);
}

TEST(YbxAccountModel, RecognizesProviderSessionCodeAndTrueHeaders) {
  EXPECT_TRUE(isProviderSessionBody("books_consent_required"));
  EXPECT_FALSE(isProviderSessionBody("invalid_token"));
  EXPECT_FALSE(isProviderSessionBody(""));
  EXPECT_TRUE(headerIsTrue("true"));
  EXPECT_TRUE(headerIsTrue("True"));
  EXPECT_TRUE(headerIsTrue("1"));
  EXPECT_FALSE(headerIsTrue("TRUE"));
  EXPECT_FALSE(headerIsTrue("false"));
  EXPECT_FALSE(headerIsTrue(""));
}

TEST(YbxAccountModel, ClampsPairingTimersToOauthFloors) {
  EXPECT_EQ(clampPollIntervalMs(5), 5000UL);
  EXPECT_EQ(clampPollIntervalMs(3), 3000UL);
  EXPECT_EQ(clampPollIntervalMs(1), 3000UL);
  EXPECT_EQ(clampPollIntervalMs(-2), 3000UL);
  EXPECT_EQ(clampCodeLifetimeMs(900), 900000UL);
  EXPECT_EQ(clampCodeLifetimeMs(30), 60000UL);
  EXPECT_EQ(clampCodeLifetimeMs(0), 60000UL);
  EXPECT_EQ(slowedDownIntervalMs(5000), 10000UL);
}

TEST(YbxAccountModel, DeadlineComparisonSurvivesMillisWrap) {
  EXPECT_FALSE(deadlineReached(1000, 2000));
  EXPECT_TRUE(deadlineReached(2000, 2000));
  EXPECT_TRUE(deadlineReached(2001, 2000));
  // The deadline wrapped past UINT32_MAX and now just passed it: reached.
  EXPECT_TRUE(deadlineReached(UINT32_C(40), UINT32_MAX - 10));
  // now far below a future deadline, but less than half a wrap away: the
  // uint32 difference is negative as a signed delta, so not reached.
  EXPECT_FALSE(deadlineReached(100, 5000));
}

TEST(YbxAccountModel, MapsPollErrorCodes) {
  EXPECT_EQ(pollOutcomeFromErrorCode("authorization_pending"), PollOutcome::Pending);
  EXPECT_EQ(pollOutcomeFromErrorCode("slow_down"), PollOutcome::SlowDown);
  EXPECT_EQ(pollOutcomeFromErrorCode("expired_token"), PollOutcome::Expired);
  EXPECT_EQ(pollOutcomeFromErrorCode("access_denied"), PollOutcome::Denied);
  EXPECT_EQ(pollOutcomeFromErrorCode("network_error"), PollOutcome::Pending);
}

TEST(YbxAccountModel, SummaryTracksConnectedProvidersFromListUrls) {
  Summary summary;
  EXPECT_EQ(summary.totalLists, 0);
  EXPECT_EQ(summary.connectedCount(), 0);
  summary.addList(ListKind::None);
  EXPECT_EQ(summary.totalLists, 0);

  summary.addList(ListKind::Yandex);
  summary.addList(ListKind::Yandex);
  summary.addList(ListKind::Litres);
  EXPECT_TRUE(summary.connected(SourceKind::Yandex));
  EXPECT_FALSE(summary.connected(SourceKind::Wb));
  EXPECT_TRUE(summary.connected(SourceKind::Litres));
  EXPECT_EQ(summary.shelfCount(SourceKind::Yandex), 2);
  EXPECT_EQ(summary.shelfCount(SourceKind::Wb), 0);
  EXPECT_EQ(summary.shelfCount(SourceKind::Litres), 1);
  EXPECT_EQ(summary.totalLists, 3);
  EXPECT_EQ(summary.connectedCount(), 2);
  EXPECT_EQ(summary.connectedMask, 0b101);

  summary.addList(ListKind::Wb);
  EXPECT_EQ(summary.connectedCount(), 3);
  EXPECT_EQ(summary.connectedMask, 0b111);

  summary.clear();
  EXPECT_EQ(summary.connectedCount(), 0);
  EXPECT_EQ(summary.totalLists, 0);
}

TEST(YbxAccountModel, SummarySaturatesCountersInsteadOfOverflowing) {
  Summary summary;
  for (int i = 0; i < 3; ++i) summary.addList(ListKind::Wb);
  summary.sources[static_cast<size_t>(SourceKind::Wb)].lists = UINT16_MAX;
  summary.totalLists = UINT16_MAX;
  summary.addList(ListKind::Wb);
  EXPECT_EQ(summary.shelfCount(SourceKind::Wb), UINT16_MAX);
  EXPECT_EQ(summary.totalLists, UINT16_MAX);
}

TEST(YbxAccountModel, CabinetUrlDerivesFromListsOriginOnly) {
  char out[96];
  ASSERT_TRUE(cabinetUrlFromListsUrl("https://ybx.example/catalog/lists", out, sizeof(out)));
  EXPECT_EQ(std::string(out), "https://ybx.example/#sources");

  // The host may carry no path at all.
  ASSERT_TRUE(cabinetUrlFromListsUrl("https://ybx.example", out, sizeof(out)));
  EXPECT_EQ(std::string(out), "https://ybx.example/#sources");

  // Never offer an http downgrade or an oversized host.
  EXPECT_FALSE(cabinetUrlFromListsUrl("http://ybx.example/catalog/lists", out, sizeof(out)));
  EXPECT_FALSE(cabinetUrlFromListsUrl("https:///catalog/lists", out, sizeof(out)));
  EXPECT_FALSE(
      cabinetUrlFromListsUrl("https://abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ01234/catalog",
                             out, sizeof(out)));

  // "https://ybx.example/#sources" needs exactly 29 bytes.
  char exact[29];
  ASSERT_TRUE(cabinetUrlFromListsUrl("https://ybx.example/catalog/lists", exact, sizeof(exact)));
  EXPECT_EQ(std::string(exact), "https://ybx.example/#sources");
  char tight[28] = {};
  EXPECT_FALSE(cabinetUrlFromListsUrl("https://ybx.example/catalog/lists", tight, sizeof(tight)));
  EXPECT_EQ(tight[0], '\0');
}
