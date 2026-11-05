#include <gtest/gtest.h>

#include <string>

#include "util/YbxReadingSummary.h"

namespace {
std::string payload(std::string fields) {
  return "{\"today\":\"2026-10-28\",\"timezone\":\"Europe/Minsk\"," + fields + "}";
}
TEST(YbxReadingSummary, AccountWeekStartsMondayAndUnknownFutureDaysStayUnknown) {
  ybx::ReadingSummary out;
  ASSERT_TRUE(ybx::parseReadingSummary(
      payload(
          R"("today_minutes":{"value":12},"week_minutes":{"value":32},"current_streak_days":{"value":3},"week_days":[{"date":"2026-10-26","minutes":20,"sources":["reader"]},{"date":"2026-10-27","minutes":0,"sources":[]},{"date":"2026-10-28","minutes":12,"sources":["yandex"]}])"),
      out));
  EXPECT_TRUE(out.available);
  EXPECT_EQ(out.todayMinutes, 12);
  EXPECT_EQ(out.weekMinutes, 32);
  EXPECT_EQ(out.streakDays, 3);
  EXPECT_EQ(out.finishedBooks, -1);
  EXPECT_EQ(out.days, (std::array<int32_t, 7>{20, -1, 12, -1, -1, -1, -1}));
}
TEST(YbxReadingSummary, NullIsUnknownButMeasuredZeroIsKnown) {
  ybx::ReadingSummary out;
  ASSERT_TRUE(ybx::parseReadingSummary(
      payload(R"("today_minutes":{"value":null},"week_minutes":{"value":0},"current_streak_days":{"value":false})"),
      out));
  EXPECT_EQ(out.todayMinutes, -1);
  EXPECT_EQ(out.weekMinutes, 0);
  EXPECT_EQ(out.streakDays, -1);
}
TEST(YbxReadingSummary, RejectsNegativeOverflowAndStringMetricsWithoutInventingValues) {
  ybx::ReadingSummary out;
  ASSERT_TRUE(ybx::parseReadingSummary(
      payload(
          R"("today_minutes":{"value":-1},"week_minutes":{"value":4294967295},"current_streak_days":{"value":"5"})"),
      out));
  EXPECT_EQ(out.todayMinutes, -1);
  EXPECT_EQ(out.weekMinutes, -1);
  EXPECT_EQ(out.streakDays, -1);
}
TEST(YbxReadingSummary, InvalidOrTruncatedResponseDoesNotReplaceExistingSummary) {
  ybx::ReadingSummary out;
  out.todayMinutes = 123;
  for (const auto input :
       {"{}", "{\"today\":\"2026-02-30\",\"timezone\":\"UTC\"}", "{\"today\":\"2026-10-28\"", "[]"}) {
    EXPECT_FALSE(ybx::parseReadingSummary(input, out));
    EXPECT_EQ(out.todayMinutes, 123);
  }
}
TEST(YbxReadingSummary, DuplicateUnobservedOrOutOfWeekDaysAreRejected) {
  ybx::ReadingSummary out;
  EXPECT_FALSE(ybx::parseReadingSummary(
      payload(R"("week_days":[{"date":"2026-10-26","sources":[]},{"date":"2026-10-26","sources":[]}])"), out));
  EXPECT_FALSE(ybx::parseReadingSummary(
      payload(R"("week_days":[{"date":"2026-10-29","minutes":10,"sources":["reader"]}])"), out));
  EXPECT_FALSE(ybx::parseReadingSummary(
      payload(R"("week_days":[{"date":"2026-10-25","minutes":10,"sources":["reader"]}])"), out));
}
TEST(YbxReadingSummary, MondayAtMonthBoundaryUsesAccountDates) {
  ybx::ReadingSummary out;
  ASSERT_TRUE(ybx::parseReadingSummary(
      R"({"today":"2026-11-01","timezone":"Pacific/Kiritimati","week_days":[{"date":"2026-10-26","minutes":1,"sources":["reader"]},{"date":"2026-11-01","minutes":9,"sources":["reader"]}]})",
      out));
  EXPECT_EQ(out.days[0], 1);
  EXPECT_EQ(out.days[6], 9);
}
TEST(YbxReadingSummary, ResponseLimitAndCivilDates) {
  ybx::ReadingSummary out;
  EXPECT_FALSE(ybx::parseReadingSummary(std::string(8193, ' '), out));
  EXPECT_EQ(ybx::civilDay("1970-01-01"), 0);
  EXPECT_EQ(ybx::civilDay("2000-02-29") + 1, ybx::civilDay("2000-03-01"));
  EXPECT_EQ(ybx::civilDay("2100-02-29"), -1);
  EXPECT_EQ(ybx::civilDay("2026-1x-01"), -1);
}

TEST(YbxReadingSummary, CacheIsBoundToCurrentTokenAndSchema) {
  ybx::ReadingSummary out;
  const auto valid =
      payload(R"("schema":1,"binding":"current-token-hash","fetched_at":1793196000,"today_minutes":{"value":7})");
  ASSERT_TRUE(ybx::parseCachedReadingSummary(valid, "current-token-hash", out));
  EXPECT_EQ(out.todayMinutes, 7);
  EXPECT_EQ(out.fetchedAt, 1793196000);
  EXPECT_FALSE(ybx::parseCachedReadingSummary(valid, "different-account-token", out));
  EXPECT_FALSE(ybx::parseCachedReadingSummary(valid, "", out));
  EXPECT_FALSE(ybx::parseCachedReadingSummary(payload(R"("schema":2,"binding":"current-token-hash")"),
                                              "current-token-hash", out));
  EXPECT_EQ(out.todayMinutes, 7);
}
}  // namespace
