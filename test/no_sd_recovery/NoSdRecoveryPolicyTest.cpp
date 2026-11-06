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

TEST(NoSdRecovery, AutoSleepTriggersAfterInactivity) {
  EXPECT_FALSE(nosd::shouldAutoSleep(10000u, 10000u, 300000u));
  EXPECT_FALSE(nosd::shouldAutoSleep(309999u, 10000u, 300000u));
  EXPECT_TRUE(nosd::shouldAutoSleep(310000u, 10000u, 300000u));
}

TEST(NoSdRecovery, AutoSleepHandlesWraparound) {
  EXPECT_FALSE(nosd::shouldAutoSleep(100u, UINT32_MAX - 50u, 300000u));
  EXPECT_TRUE(nosd::shouldAutoSleep(300000u, UINT32_MAX - 50u, 300000u));
}

TEST(NoSdRecovery, PowerButtonHeldTriggersSleep) {
  EXPECT_FALSE(nosd::shouldPowerSleep(false, 1000u, 500u));
  EXPECT_FALSE(nosd::shouldPowerSleep(true, 400u, 500u));
  EXPECT_TRUE(nosd::shouldPowerSleep(true, 501u, 500u));
}

