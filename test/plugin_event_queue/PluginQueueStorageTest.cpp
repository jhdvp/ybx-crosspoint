#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <string_view>

#include "util/PluginQueueStorage.h"

namespace {
struct PowerCut {};
struct Files {
  std::map<std::string, std::string> data;
  int failAt = 0;
  int cutAfter = 0;
  int operations = 0;
  bool exists(const std::string& path) { return data.count(path) != 0; }
  bool before() { return ++operations != failAt; }
  void after() {
    if (operations == cutAfter) throw PowerCut{};
  }
  bool remove(const std::string& path) {
    if (!before()) return false;
    if (!data.erase(path)) return false;
    after();
    return true;
  }
  bool rename(const std::string& from, const std::string& to) {
    if (!before() || !exists(from) || exists(to)) return false;
    data[to] = data.at(from);
    data.erase(from);
    after();
    return true;
  }
  bool writeComplete(const std::string& path, std::string_view content) {
    if (!before()) {
      data[path] = std::string(content.substr(0, content.size() / 2));
      return false;
    }
    data[path] = content;
    after();
    return true;
  }
};
constexpr const char* path = "/events.jsonl";

TEST(PluginQueueStorage, SuccessfulReplacementCleansJournal) {
  Files files;
  files.data[path] = "delivered\nwaiting\n";
  EXPECT_TRUE(pluginevents::replaceQueue(files, path, "waiting\n"));
  EXPECT_EQ(files.data, (std::map<std::string, std::string>{{path, "waiting\n"}}));
}

TEST(PluginQueueStorage, EveryFailedWriteOrRenameRetainsUndeliveredEvents) {
  for (int failure = 1; failure <= 4; ++failure) {
    SCOPED_TRACE(failure);
    Files files;
    files.data[path] = "delivered\nwaiting\n";
    files.failAt = failure;
    EXPECT_FALSE(pluginevents::replaceQueue(files, path, "waiting\n"));
    files.failAt = 0;
    EXPECT_TRUE(pluginevents::recoverQueue(files, path));
    ASSERT_TRUE(files.exists(path));
    EXPECT_NE(files.data.at(path).find("waiting\n"), std::string::npos);
    EXPECT_EQ(files.data.size(), 1);
  }
}

TEST(PluginQueueStorage, PowerCutAtEachTransactionBoundaryRecoversOldOrNewQueue) {
  for (int cut = 1; cut <= 4; ++cut) {
    SCOPED_TRACE(cut);
    Files files;
    files.data[path] = "delivered\nwaiting\n";
    files.cutAfter = cut;
    EXPECT_THROW(pluginevents::replaceQueue(files, path, "waiting\n"), PowerCut);
    files.cutAfter = 0;
    ASSERT_TRUE(pluginevents::recoverQueue(files, path));
    ASSERT_TRUE(files.exists(path));
    EXPECT_TRUE(files.data.at(path) == "delivered\nwaiting\n" || files.data.at(path) == "waiting\n");
    EXPECT_EQ(files.data.size(), 1);
  }
}

TEST(PluginQueueStorage, IncompletePendingNeverBecomesQueue) {
  Files files;
  files.data[std::string(path) + ".pending"] = "truncated";
  EXPECT_TRUE(pluginevents::recoverQueue(files, path));
  EXPECT_TRUE(files.data.empty());
}

TEST(PluginQueueStorage, FailedRecoveryRetainsBackupForNextAttempt) {
  Files files;
  files.data[std::string(path) + ".bak"] = "waiting\n";
  files.data[std::string(path) + ".pending"] = "partial";
  files.failAt = 1;
  EXPECT_FALSE(pluginevents::recoverQueue(files, path));
  EXPECT_EQ(files.data.at(std::string(path) + ".bak"), "waiting\n");
  files.failAt = 0;
  ASSERT_TRUE(pluginevents::recoverQueue(files, path));
  EXPECT_EQ(files.data.at(path), "waiting\n");
}

TEST(PluginQueueStorage, FailedRollbackKeepsBackupWithoutDeletingIt) {
  struct RenameFailureFiles : Files {
    bool rename(const std::string& from, const std::string& to) {
      if (to == path) return false;
      return Files::rename(from, to);
    }
  } files;
  files.data[path] = "waiting\n";
  EXPECT_FALSE(pluginevents::replaceQueue(files, path, "new\n"));
  EXPECT_EQ(files.data.at(std::string(path) + ".bak"), "waiting\n");
  Files recovered;
  recovered.data = files.data;
  ASSERT_TRUE(pluginevents::recoverQueue(recovered, path));
  EXPECT_EQ(recovered.data.at(path), "waiting\n");
}

struct TailFile {
  std::string data;
  size_t pos = 0;
  bool readFails = false;
  bool truncateFails = false;
  size_t fileSize() { return data.size(); }
  bool seek(size_t offset) {
    pos = offset;
    return offset <= data.size();
  }
  int read(void* out, size_t count) {
    if (readFails) return -1;
    count = std::min(count, data.size() - pos);
    std::memcpy(out, data.data() + pos, count);
    pos += count;
    return static_cast<int>(count);
  }
  bool truncate(size_t count) {
    if (truncateFails) return false;
    data.resize(count);
    return true;
  }
};

TEST(PluginQueueStorage, RepairsEveryPossibleTornAppendWithoutLosingCompleteLines) {
  const std::string previous = "first complete line\nsecond complete line\n";
  const std::string event(511, 'x');
  for (size_t torn = 1; torn <= event.size(); ++torn) {
    SCOPED_TRACE(torn);
    TailFile file{previous + event.substr(0, torn)};
    ASSERT_TRUE(pluginevents::repairQueueTail(file, 512));
    EXPECT_EQ(file.data, previous);
  }
}

TEST(PluginQueueStorage, CompleteTailAndEmptyQueueStayIntact) {
  TailFile file{"complete\n"};
  EXPECT_TRUE(pluginevents::repairQueueTail(file, 512));
  EXPECT_EQ(file.data, "complete\n");
  file.data.clear();
  EXPECT_TRUE(pluginevents::repairQueueTail(file, 512));
}

TEST(PluginQueueStorage, OnlyPartialFirstEventCanBeTruncatedToEmpty) {
  TailFile file{"partial"};
  EXPECT_TRUE(pluginevents::repairQueueTail(file, 512));
  EXPECT_TRUE(file.data.empty());
  file.data.assign(600, 'x');
  EXPECT_FALSE(pluginevents::repairQueueTail(file, 512));
  EXPECT_EQ(file.data.size(), 600);
}

TEST(PluginQueueStorage, TailReadAndTruncateFailuresStopAppend) {
  TailFile file{"complete\npartial"};
  file.readFails = true;
  EXPECT_FALSE(pluginevents::repairQueueTail(file, 512));
  EXPECT_EQ(file.data, "complete\npartial");
  file.readFails = false;
  file.truncateFails = true;
  EXPECT_FALSE(pluginevents::repairQueueTail(file, 512));
  EXPECT_EQ(file.data, "complete\npartial");
}
}  // namespace
