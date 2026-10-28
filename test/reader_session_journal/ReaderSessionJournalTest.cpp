#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>
#include <string_view>

#include "util/ReaderSessionJournal.h"

namespace {

constexpr const char* PATH = "/.crosspoint/reader-session.journal";
const std::string PENDING = std::string(PATH) + ".pending";

struct Files {
  std::map<std::string, std::string> data;
  int failAt = 0;
  int operations = 0;
  bool before() { return ++operations != failAt; }
  bool exists(const std::string& path) { return data.count(path) != 0; }
  bool remove(const std::string& path) {
    if (!before()) return false;
    return data.erase(path) != 0;
  }
  bool rename(const std::string& from, const std::string& to) {
    if (!before() || !exists(from) || exists(to)) return false;
    data[to] = data.at(from);
    data.erase(from);
    return true;
  }
  bool writeComplete(const std::string& path, std::string_view content) {
    if (!before()) return false;
    data[path] = std::string(content);
    return true;
  }
  bool read(const std::string& path, std::string& out, size_t cap) {
    const auto it = data.find(path);
    if (it == data.end() || it->second.size() > cap) return false;
    out = it->second;
    return true;
  }
};

readersession::Record record(const char* id = "a1b2c3d4-7", uint32_t duration = 600) {
  readersession::Record out;
  std::snprintf(out.id, sizeof(out.id), "%s", id);
  out.startEpoch = 1730000000;
  out.durationSeconds = duration;
  out.startProgressBp = 1200;
  out.endProgressBp = 3400;
  out.book = "/books/ybx/42 - Война и мир.epub";
  return out;
}

TEST(ReaderSessionJournal, RoundTripPreservesIdAndSummary) {
  Files files;
  const readersession::Record written = record();
  ASSERT_TRUE(readersession::write(files, PATH, written));

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_STREQ(read.id, written.id);
  EXPECT_EQ(read.startEpoch, written.startEpoch);
  EXPECT_EQ(read.durationSeconds, written.durationSeconds);
  EXPECT_EQ(read.startProgressBp, written.startProgressBp);
  EXPECT_EQ(read.endProgressBp, written.endProgressBp);
  EXPECT_EQ(read.book, written.book);
  EXPECT_EQ(files.data.size(), 1u);
}

TEST(ReaderSessionJournal, NewerPendingSurvivesAPowerCutBeforeTheSwap) {
  Files files;
  files.data[PATH] = readersession::serialize(record("a1b2c3d4-7", 600));
  // A checkpoint wrote the complete pending file but lost power before it
  // could remove the live journal.
  files.data[PENDING] = readersession::serialize(record("a1b2c3d4-7", 900));

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_EQ(read.durationSeconds, 900u);
}

TEST(ReaderSessionJournal, PendingCheckpointOfANewSessionWinsOverALongerOldRecord) {
  Files files;
  // An earlier session's unsent 900 s checkpoint is still live and the current
  // session staged its first 60 s checkpoint on top of it: the staged copy is
  // the newer write, so it must win even though it is shorter.
  files.data[PATH] = readersession::serialize(record("a1b2c3d4-7", 900));
  files.data[PENDING] = readersession::serialize(record("e5f60718-9", 60));

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_STREQ(read.id, "e5f60718-9");
  EXPECT_EQ(read.durationSeconds, 60u);
}

TEST(ReaderSessionJournal, CompletePendingIsRecoveredWithoutALiveJournal) {
  Files files;
  files.data[PENDING] = readersession::serialize(record("a1b2c3d4-9", 42));

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_STREQ(read.id, "a1b2c3d4-9");
  EXPECT_EQ(read.durationSeconds, 42u);
}

TEST(ReaderSessionJournal, TornPendingNeverReplacesTheLiveJournal) {
  Files files;
  ASSERT_TRUE(readersession::write(files, PATH, record("a1b2c3d4-7", 600)));
  files.data[PENDING] = "CPRS1\na1b2c3";

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_EQ(read.durationSeconds, 600u);
}

TEST(ReaderSessionJournal, CorruptOrOversizedRecordIsRejected) {
  const std::string valid = readersession::serialize(record());
  readersession::Record read;

  EXPECT_FALSE(readersession::parse("", read));
  EXPECT_FALSE(readersession::parse("NOPE1\n" + valid.substr(6), read));
  EXPECT_FALSE(readersession::parse("CPRS1\nzzzzzzzz-1\n0\n60\n0\n0\n/book", read));
  EXPECT_FALSE(readersession::parse("CPRS1\na1b2c3d4-7\n0\n60\n0\n0\n", read));
  EXPECT_FALSE(readersession::parse("CPRS1\na1b2c3d4-7\n0\n0\n0\n0\n/book", read));
  EXPECT_FALSE(readersession::parse("CPRS1\na1b2c3d4-7\n0\n60\n0\n10001\n/book", read));
  EXPECT_FALSE(readersession::parse("CPRS1\na1b2c3d4-7\n0\n60\n0\n0\n/bad\nbook", read));
  EXPECT_TRUE(readersession::parse(valid, read));
}

TEST(ReaderSessionJournal, BookPathIsBounded) {
  readersession::Record tooLong = record();
  tooLong.book.assign(readersession::MAX_BOOK_PATH_LENGTH + 1, 'x');
  EXPECT_FALSE(readersession::valid(tooLong));
}

TEST(ReaderSessionJournal, FailedSwapKeepsBothCopiesRecoverable) {
  Files files;
  ASSERT_TRUE(readersession::write(files, PATH, record("a1b2c3d4-7", 600)));
  files.failAt = files.operations + 3;  // the final rename fails after the live copy is dropped
  EXPECT_FALSE(readersession::write(files, PATH, record("a1b2c3d4-7", 900)));
  EXPECT_TRUE(files.exists(PENDING));

  readersession::Record read;
  ASSERT_TRUE(readersession::read(files, PATH, read));
  EXPECT_EQ(read.durationSeconds, 900u);
}

TEST(ReaderSessionJournal, RemoveClearsBothCopies) {
  Files files;
  files.data[PATH] = "live";
  files.data[PENDING] = "pending";
  EXPECT_TRUE(readersession::remove(files, PATH));
  EXPECT_TRUE(files.data.empty());
}

}  // namespace
