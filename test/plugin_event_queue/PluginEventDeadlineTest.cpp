#include <gtest/gtest.h>

#include "util/PluginEventDeadline.h"

TEST(PluginEventDeadline, StopsAtExactDeadline) {
  const pluginevents::EventDeadline deadline(100, 2000);
  EXPECT_EQ(deadline.remaining(100), 2000);
  EXPECT_EQ(deadline.remaining(2099), 1);
  EXPECT_EQ(deadline.remaining(2100), 0);
  EXPECT_EQ(deadline.remaining(2500), 0);
}

TEST(PluginEventDeadline, HandlesMillisRollover) {
  const pluginevents::EventDeadline deadline(UINT32_MAX - 9, 20);
  EXPECT_EQ(deadline.remaining(UINT32_MAX), 11);
  EXPECT_EQ(deadline.remaining(0), 10);
  EXPECT_EQ(deadline.remaining(9), 1);
  EXPECT_EQ(deadline.remaining(10), 0);
}

TEST(PluginEventDeadline, ZeroPreservesUnlimitedOnlineDrain) {
  const pluginevents::EventDeadline deadline(100, 0);
  EXPECT_EQ(deadline.remaining(100), UINT32_MAX);
  EXPECT_EQ(deadline.remaining(UINT32_MAX), UINT32_MAX);
}

TEST(PluginEventDeadline, RetryDoesNotResetHandlerBudget) {
  const pluginevents::EventDeadline deadline(100, 2000);
  const auto firstRequestTimeout = deadline.remaining(100);
  const auto refreshTimeout = deadline.remaining(1100);
  const auto retryTimeout = deadline.remaining(2000);
  EXPECT_EQ(firstRequestTimeout, 2000);
  EXPECT_EQ(refreshTimeout, 1000);
  EXPECT_EQ(retryTimeout, 100);
  EXPECT_EQ(deadline.remaining(2100), 0);
}
