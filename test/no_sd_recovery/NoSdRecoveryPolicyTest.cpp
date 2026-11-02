#include <gtest/gtest.h>
#include "util/NoSdRecoveryPolicy.h"

TEST(NoSdRecovery, NeverProbesWithoutUserRequest) {
  nosd::RetryGate gate;
  for (uint32_t now : {0u, 1000u, 5000u, 60000u, 100000u}) {
    EXPECT_FALSE(gate.consume(now, false));
  }
  EXPECT_FALSE(gate.attempted);
}
TEST(NoSdRecovery, FirstButtonOrTouchRequestCanRetryImmediately) {
  nosd::RetryGate gate;
  EXPECT_TRUE(gate.consume(0u, true));
  EXPECT_TRUE(gate.attempted);
}
TEST(NoSdRecovery, RepeatedRequestsAreThrottled) {
  nosd::RetryGate gate;
  EXPECT_TRUE(gate.consume(20u, true));
  EXPECT_FALSE(gate.consume(21u, true));
  EXPECT_FALSE(gate.consume(2019u, true));
  EXPECT_TRUE(gate.consume(2020u, true));
}
TEST(NoSdRecovery, MillisWraparoundDoesNotLockOutUser) {
  nosd::RetryGate gate;
  EXPECT_TRUE(gate.consume(UINT32_MAX - 100u, true));
  EXPECT_FALSE(gate.consume(15u, true));
  EXPECT_TRUE(gate.consume(1910u, true));
}
TEST(NoSdRecovery, IdleAfterFailedAttemptNeverPollsAutomatically) {
  nosd::RetryGate gate;
  EXPECT_TRUE(gate.consume(1000u, true));
  EXPECT_FALSE(gate.consume(7000u, false));
  EXPECT_TRUE(gate.consume(7000u, true));
}
