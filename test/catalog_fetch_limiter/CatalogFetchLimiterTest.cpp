#include <gtest/gtest.h>

#include "src/network/CatalogFetchLimiter.h"

using freeink::network::CatalogFetchLimiter;

namespace {
constexpr uint32_t kBudget = CatalogFetchLimiter::DEFAULT_BUDGET_MS;
}

TEST(CatalogFetchLimiterTest, InactiveUntilBegin) {
  CatalogFetchLimiter limiter;
  EXPECT_FALSE(limiter.active());
  EXPECT_EQ(CatalogFetchLimiter::Stop::None, limiter.stop());
  EXPECT_FALSE(limiter.abortRequested(1000));
  EXPECT_EQ(0u, limiter.socketTimeoutMs(1000));
  EXPECT_EQ(0u, limiter.remainingMs(1000));
}

TEST(CatalogFetchLimiterTest, AllowsWorkUntilTheAbsoluteDeadline) {
  CatalogFetchLimiter limiter;
  limiter.begin(1000);
  EXPECT_TRUE(limiter.active());

  EXPECT_FALSE(limiter.abortRequested(1000 + kBudget - 1));
  EXPECT_EQ(CatalogFetchLimiter::Stop::None, limiter.stop());
  EXPECT_EQ(1u, limiter.remainingMs(1000 + kBudget - 1));

  EXPECT_TRUE(limiter.abortRequested(1000 + kBudget));
  EXPECT_EQ(CatalogFetchLimiter::Stop::TimedOut, limiter.stop());
}

TEST(CatalogFetchLimiterTest, TimeoutLatchStaysLatched) {
  CatalogFetchLimiter limiter;
  limiter.begin(0);
  ASSERT_TRUE(limiter.abortRequested(kBudget));
  EXPECT_TRUE(limiter.abortRequested(kBudget + 60000));
  EXPECT_EQ(CatalogFetchLimiter::Stop::TimedOut, limiter.stop());
}

TEST(CatalogFetchLimiterTest, CancelWinsAndStaysLatched) {
  CatalogFetchLimiter limiter;
  limiter.begin(0);
  limiter.cancel();
  EXPECT_TRUE(limiter.abortRequested(0));
  EXPECT_EQ(CatalogFetchLimiter::Stop::Cancelled, limiter.stop());
  // Past the deadline the reason stays the user's cancel.
  EXPECT_TRUE(limiter.abortRequested(kBudget + 1));
  EXPECT_EQ(CatalogFetchLimiter::Stop::Cancelled, limiter.stop());
}

TEST(CatalogFetchLimiterTest, EndResetsAndBeginStartsAFreshWindow) {
  CatalogFetchLimiter limiter;
  limiter.begin(1000);
  limiter.cancel();
  limiter.end();
  EXPECT_FALSE(limiter.active());
  EXPECT_EQ(CatalogFetchLimiter::Stop::None, limiter.stop());
  EXPECT_FALSE(limiter.abortRequested(2000));
  EXPECT_EQ(0u, limiter.socketTimeoutMs(2000));

  limiter.begin(5000);
  EXPECT_TRUE(limiter.active());
  EXPECT_EQ(CatalogFetchLimiter::Stop::None, limiter.stop());
  EXPECT_FALSE(limiter.abortRequested(5000));
  EXPECT_EQ(kBudget, limiter.remainingMs(5000));
}

TEST(CatalogFetchLimiterTest, CustomBudget) {
  CatalogFetchLimiter limiter;
  limiter.begin(500, 100);
  EXPECT_FALSE(limiter.abortRequested(599));
  EXPECT_TRUE(limiter.abortRequested(600));
  EXPECT_EQ(CatalogFetchLimiter::Stop::TimedOut, limiter.stop());
}

TEST(CatalogFetchLimiterTest, DeadlineMathSurvivesCounterWraparound) {
  // 0xFFFFFFF0 + 20000 wraps past UINT32_MAX; the deadline becomes 0x4E10.
  CatalogFetchLimiter limiter;
  limiter.begin(0xFFFFFFF0u);
  EXPECT_FALSE(limiter.abortRequested(0xFFFFFFF0u));
  EXPECT_EQ(19984u, limiter.remainingMs(0x00000000u));
  EXPECT_FALSE(limiter.abortRequested(0x00000000u));
  EXPECT_FALSE(limiter.abortRequested(0x4E0Fu));
  EXPECT_TRUE(limiter.abortRequested(0x4E10u));
  EXPECT_EQ(CatalogFetchLimiter::Stop::TimedOut, limiter.stop());
}

TEST(CatalogFetchLimiterTest, SocketTimeoutIsClampedToTheRemainingBudget) {
  CatalogFetchLimiter limiter;
  limiter.begin(0);
  // Fresh window: each socket operation is capped well below the budget so a
  // single stalled read cannot consume all of it.
  EXPECT_EQ(CatalogFetchLimiter::MAX_SOCKET_TIMEOUT_MS, limiter.socketTimeoutMs(0));
  EXPECT_EQ(5000u, limiter.socketTimeoutMs(15000));
  EXPECT_EQ(1u, limiter.socketTimeoutMs(kBudget - 1));
  // Expired but not yet polled: still ask for a minimal timeout, never zero
  // (zero means "default" to the HTTP layer).
  EXPECT_EQ(1u, limiter.socketTimeoutMs(kBudget));

  limiter.abortRequested(kBudget);
  EXPECT_EQ(0u, limiter.socketTimeoutMs(kBudget));
}
