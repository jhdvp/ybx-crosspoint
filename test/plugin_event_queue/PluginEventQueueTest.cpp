#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/PluginEventQueue.h"

namespace {
bool isSnapshot(std::string_view line) { return line.front() == 'S'; }

TEST(PluginEventQueue, ReservesLastAttemptForNewestSnapshot) {
  std::string queue = "A\nB\nS1\nC\nD\nS2\n";
  size_t budget = 4;
  std::vector<std::string> delivered;
  EXPECT_TRUE(pluginevents::drainQueue(queue, budget, isSnapshot, [&](std::string_view line) {
    delivered.emplace_back(line);
    return true;
  }));
  EXPECT_EQ(delivered, (std::vector<std::string>{"A", "B", "C", "S2"}));
  EXPECT_EQ(queue, "D\n");
  EXPECT_EQ(budget, 0);
}

TEST(PluginEventQueue, FailedTelemetryDoesNotBlockSnapshotOrLoseSessions) {
  std::string queue = "A\nS1\nB\nS2\n";
  size_t budget = 4;
  std::vector<std::string> delivered;
  pluginevents::drainQueue(queue, budget, isSnapshot, [&](std::string_view line) {
    delivered.emplace_back(line);
    return line.front() == 'S';
  });
  EXPECT_EQ(delivered, (std::vector<std::string>{"A", "S2"}));
  EXPECT_EQ(queue, "A\nB\n");
  EXPECT_EQ(budget, 2);
}

TEST(PluginEventQueue, FailedSnapshotKeepsAllSnapshotsForRetry) {
  std::string queue = "S1\nA\nS2";
  size_t budget = 4;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view line) { return line == "A"; });
  EXPECT_EQ(queue, "S1\nS2");
  budget = 1;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view line) { return line == "S2"; });
  EXPECT_TRUE(queue.empty());
}

TEST(PluginEventQueue, ZeroBudgetLeavesBytesUntouched) {
  std::string queue = "S1\nA\nS2";
  size_t budget = 0;
  EXPECT_FALSE(pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view) {
    ADD_FAILURE() << "Must not deliver with zero budget";
    return true;
  }));
  EXPECT_EQ(queue, "S1\nA\nS2");
}

TEST(PluginEventQueue, OneAttemptPrioritizesSnapshotAndRetainsOrdinaryEvents) {
  std::string queue = "A\nS1\nB\nS2\n";
  size_t budget = 1;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view line) {
    EXPECT_EQ(line, "S2");
    return true;
  });
  EXPECT_EQ(queue, "A\nB\n");
}

TEST(PluginEventQueue, LegacyManifestKeepsStrictFifoOnFailure) {
  std::string queue = "A\nS1\nB\nS2\n";
  size_t budget = 4;
  std::vector<std::string> delivered;
  pluginevents::drainQueue(
      queue, budget, [](std::string_view) { return false; },
      [&](std::string_view line) {
        delivered.emplace_back(line);
        return line != "S1";
      });
  EXPECT_EQ(delivered, (std::vector<std::string>{"A", "S1"}));
  EXPECT_EQ(queue, "S1\nB\nS2\n");
  EXPECT_EQ(budget, 2);
}

TEST(PluginEventQueue, DrainingWithoutSnapshotsHonorsBudgetAndFinalNewline) {
  std::string queue = "A\nB\nC";
  size_t budget = 2;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view) { return true; });
  EXPECT_EQ(queue, "C");
  budget = 2;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view) { return true; });
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(budget, 1);
}

TEST(PluginEventQueue, EmptyLinesDoNotConsumeNetworkBudget) {
  std::string queue = "\nA\n\nS1\n\n";
  size_t budget = 2;
  pluginevents::drainQueue(queue, budget, isSnapshot, [](std::string_view) { return true; });
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(budget, 0);
}
}  // namespace
