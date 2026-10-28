#include <gtest/gtest.h>

#include "util/YbxAccountModel.h"

using namespace ybxaccount;

TEST(YbxAccountModel, ClassifiesOnlyConnectedProviderCatalogs) {
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/catalog?shelf=reading&page=1"), ListKind::Yandex);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/wb/catalog?page=1"), ListKind::Wb);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app/api/v1/crosspoint/litres/catalog?page=1"), ListKind::Litres);
  EXPECT_EQ(classifyListUrl("https://example.test/unrelated"), ListKind::None);
  EXPECT_EQ(classifyListUrl("https://evil.example/crosspoint/wb/catalog"), ListKind::None);
  EXPECT_EQ(classifyListUrl("https://my.ybx.app.evil.example/api/v1/crosspoint/catalog"), ListKind::None);
}
TEST(YbxAccountModel, CountsProviderShelvesWithoutPretendingTheOthersAreLinked) {
  Summary s;
  for (int i = 0; i < 4; ++i) s.addList(ListKind::Yandex);
  s.addList(ListKind::Wb);
  EXPECT_TRUE(s.connected(SourceKind::Yandex));
  EXPECT_TRUE(s.connected(SourceKind::Wb));
  EXPECT_FALSE(s.connected(SourceKind::Litres));
  EXPECT_EQ(s.connectedCount(), 2);
  EXPECT_EQ(s.shelfCount(SourceKind::Yandex), 4);
  EXPECT_EQ(s.shelfCount(SourceKind::Wb), 1);
  EXPECT_EQ(s.totalLists, 5);
  s.clear();
  EXPECT_FALSE(s.connected(SourceKind::Yandex));
  EXPECT_EQ(s.totalLists, 0);
}
TEST(YbxAccountModel, ExpiredReaderTokenIsNotAProviderLoginFailure) {
  EXPECT_EQ(listsOutcomeFromStatus(200, false), ListsOutcome::Ok);
  EXPECT_EQ(listsOutcomeFromStatus(401, false), ListsOutcome::InvalidToken);
  EXPECT_EQ(listsOutcomeFromStatus(403, true), ListsOutcome::DeviceRemoved);
  EXPECT_EQ(listsOutcomeFromStatus(409, false), ListsOutcome::ProviderSession);
  EXPECT_EQ(listsOutcomeFromStatus(-1, false), ListsOutcome::Offline);
  EXPECT_TRUE(isProviderSessionBody("books_consent_required"));
  EXPECT_FALSE(isProviderSessionBody("invalid_token"));
}
TEST(YbxAccountModel, PollTimingIsBoundedAndHandlesMillisRollover) {
  EXPECT_EQ(clampPollIntervalMs(0), 3000UL);
  EXPECT_EQ(clampPollIntervalMs(5), 5000UL);
  EXPECT_EQ(clampPollIntervalMs(999999999L), 3600000UL);
  EXPECT_EQ(clampCodeLifetimeMs(0), 60000UL);
  EXPECT_EQ(clampCodeLifetimeMs(999999999L), 3600000UL);
  EXPECT_FALSE(deadlineReached(0xfffffff0UL, 0x00000010UL));
  EXPECT_TRUE(deadlineReached(0x00000010UL, 0x00000010UL));
  EXPECT_EQ(pollOutcomeFromErrorCode("slow_down"), PollOutcome::SlowDown);
  EXPECT_EQ(pollOutcomeFromErrorCode("access_denied"), PollOutcome::Denied);
}
