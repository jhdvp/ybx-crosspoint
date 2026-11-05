#include <gtest/gtest.h>
#include "activities/reader/ProgressWriteGate.h"

using readerprogress::ProgressWriteGate;

TEST(ProgressWriteGate, FirstRenderedPagePersistsImmediately) {
  ProgressWriteGate gate;
  EXPECT_TRUE(gate.observe(0x10002u, 100));
  gate.complete(100, true);
  EXPECT_FALSE(gate.needsFlush());
  EXPECT_FALSE(gate.observe(0x10002u, 101));
}
TEST(ProgressWriteGate, CoalescesSevenPagesAndWritesOnEighth) {
  ProgressWriteGate gate;
  ASSERT_TRUE(gate.observe(1, 10));
  gate.complete(10, true);
  for (uint64_t key=2; key<=8; ++key) EXPECT_FALSE(gate.observe(key, 20 + key));
  EXPECT_TRUE(gate.observe(9, 29));
  gate.complete(29, true);
  EXPECT_FALSE(gate.needsFlush());
}
TEST(ProgressWriteGate, IdleSaveBecomesDueAfterSixtySeconds) {
  ProgressWriteGate gate;
  ASSERT_TRUE(gate.observe(1, 1000));
  gate.complete(1000, true);
  EXPECT_FALSE(gate.observe(2, 1500));
  EXPECT_FALSE(gate.due(60999));
  EXPECT_TRUE(gate.due(61000));
}
TEST(ProgressWriteGate, ExplicitExitAndSleepCanFlushPending) {
  ProgressWriteGate gate;
  ASSERT_TRUE(gate.observe(1, 100));
  gate.complete(100, true);
  EXPECT_FALSE(gate.observe(2, 120));
  EXPECT_TRUE(gate.needsFlush());
  gate.complete(120, true);
  EXPECT_FALSE(gate.needsFlush());
}
TEST(ProgressWriteGate, FailedWriteRetainsDirtyAndBacksOff) {
  ProgressWriteGate gate;
  EXPECT_TRUE(gate.observe(1, 120));
  gate.complete(120, false);
  EXPECT_TRUE(gate.needsFlush());
  EXPECT_FALSE(gate.due(121));
  EXPECT_FALSE(gate.due(2119));
  EXPECT_TRUE(gate.due(2120));
  gate.complete(2120, true);
  EXPECT_FALSE(gate.needsFlush());
}
TEST(ProgressWriteGate, HandlesMillisWraparound) {
  ProgressWriteGate gate;
  const uint32_t nearWrap = UINT32_MAX - 900;
  ASSERT_TRUE(gate.observe(1, nearWrap));
  gate.complete(nearWrap, true);
  EXPECT_FALSE(gate.observe(2, nearWrap+100));
  EXPECT_TRUE(gate.due(59099));
}
TEST(ProgressWriteGate, RepeatedSamePositionDoesNotCountTurns) {
  ProgressWriteGate gate;
  ASSERT_TRUE(gate.observe(1, 123));
  gate.complete(123, true);
  for (int i=0;i<100;i++) EXPECT_FALSE(gate.observe(1, 124+i));
  EXPECT_FALSE(gate.needsFlush());
}
TEST(ProgressWriteGate, FailedExplicitFlushDoesNotDiscardPosition) {
  ProgressWriteGate gate;
  ASSERT_TRUE(gate.observe(1, 10));
  gate.complete(10, true);
  EXPECT_FALSE(gate.observe(2, 15));
  gate.complete(15, false);
  EXPECT_TRUE(gate.needsFlush());
  EXPECT_FALSE(gate.due(16));
  EXPECT_TRUE(gate.due(60020));
}
