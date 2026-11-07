#include <gtest/gtest.h>

#include <array>
#include <string>

#include "HalStorage.h"
#include "util/ReadingStats.h"
#include "util/ReadingStatsSnapshot.h"

namespace {
using namespace readingstats;
struct Fixture : testing::Test {
  void SetUp() override { fake_sd::disk.reset(); }
};
std::string stateFile(bool legacy) {
  State state;
  addSession(state, 1730000000, 240);
  noteSession(state, "facebeef-0000000001", 1730000000);
  std::array<uint8_t, MAX_STATE_BYTES> raw{};
  const size_t written = serializeState(state, raw.data(), raw.size());
  if (legacy) raw[4] = 1;
  return {reinterpret_cast<const char*>(raw.data()), written - (legacy ? RECENT_IDS_BYTES : 0)};
}
std::string bookFile() {
  Book books[MAX_BOOKS]{};
  int count = 0;
  addBook(books, count, "/books/a.epub", 240, 1, 20754);
  std::array<uint8_t, MAX_BOOKS_BYTES> raw{};
  const size_t written = serializeBooks(books, count, raw.data(), raw.size());
  return {reinterpret_cast<const char*>(raw.data()), written};
}
TEST_F(Fixture, MigratesCps1AndBooksAndPreservesOriginalBackup) {
  auto& disk = fake_sd::disk.files;
  disk["/.crosspoint/reading-stats.bin"] = stateFile(true);
  disk["/.crosspoint/reading-books.bin"] = bookFile();
  const auto oldState = disk["/.crosspoint/reading-stats.bin"];
  const auto oldBooks = disk["/.crosspoint/reading-books.bin"];
  ASSERT_TRUE(recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub"));
  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 420u);
  EXPECT_EQ(state.totalSessions, 2u);
  EXPECT_TRUE(isRecorded(state, "facebeef-0000000001"));
  EXPECT_TRUE(isRecorded(state, "facebeef-0000000002"));
  Book books[MAX_BOOKS]{};
  int count = 0;
  ASSERT_TRUE(loadBooks(books, MAX_BOOKS, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(books[0].seconds, 420u);
  EXPECT_EQ(books[0].sessions, 2u);
  EXPECT_EQ(disk["/.crosspoint/reading-stats.bin"], oldState);
  EXPECT_EQ(disk["/.crosspoint/reading-books.bin"], oldBooks);
}
TEST_F(Fixture, RecordSessionRepeatedOlderIdCannotDoubleCount) {
  ASSERT_TRUE(recordSession("facebeef-0000000001", 1730000000, 240, "/books/a.epub"));
  ASSERT_TRUE(recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub"));
  const auto committed = fake_sd::disk.files[snapshot::PRIMARY];
  ASSERT_TRUE(recordSession("facebeef-0000000001", 1730000000, 240, "/books/a.epub"));
  EXPECT_EQ(fake_sd::disk.files[snapshot::PRIMARY], committed);
  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 420u);
  Book books[MAX_BOOKS]{};
  int count = 0;
  ASSERT_TRUE(loadBooks(books, MAX_BOOKS, count));
  EXPECT_EQ(books[0].seconds, 420u);
}
TEST_F(Fixture, CorruptLegacyIsFailClosed) {
  fake_sd::disk.files["/.crosspoint/reading-stats.bin"] = "corrupt";
  fake_sd::disk.files["/.crosspoint/reading-books.bin"] = bookFile();
  EXPECT_FALSE(recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub"));
  EXPECT_FALSE(fake_sd::disk.files.count(snapshot::PRIMARY));
}
TEST_F(Fixture, FailedWritesRetainMatchingBookAndAggregateCounters) {
  ASSERT_TRUE(recordSession("facebeef-0000000001", 1730000000, 240, "/books/a.epub"));
  const auto initial = fake_sd::disk.files;
  for (int i = 1; i < 10; ++i) {
    fake_sd::disk.files = initial;
    fake_sd::disk.operations = 0;
    fake_sd::disk.failAt = i;
    fake_sd::disk.tearWrite = true;
    (void)recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub");
    fake_sd::disk.failAt = -1;
    State state;
    ASSERT_TRUE(loadState(state)) << i;
    Book books[MAX_BOOKS]{};
    int count = 0;
    ASSERT_TRUE(loadBooks(books, MAX_BOOKS, count)) << i;
    ASSERT_EQ(count, 1);
    EXPECT_EQ(state.totalSeconds, books[0].seconds) << i;
    EXPECT_EQ(state.totalSessions, books[0].sessions) << i;
    EXPECT_TRUE(state.totalSeconds == 240 || state.totalSeconds == 420) << i;
  }
}
TEST_F(Fixture, BadPrimaryRestoresBackupWithoutInflatingBookTotals) {
  ASSERT_TRUE(recordSession("facebeef-0000000001", 1730000000, 240, "/books/a.epub"));
  ASSERT_TRUE(recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub"));
  fake_sd::disk.files[snapshot::PRIMARY].back() ^= 0x08;
  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 240u);
  ASSERT_TRUE(recordSession("facebeef-0000000002", 1730000400, 180, "/books/a.epub"));
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 420u);
  Book books[MAX_BOOKS]{};
  int count = 0;
  ASSERT_TRUE(loadBooks(books, MAX_BOOKS, count));
  EXPECT_EQ(books[0].seconds, 420u);
}
TEST_F(Fixture, NoSdDoesNotCreateNewHistory) {
  fake_sd::disk.available = false;
  EXPECT_FALSE(recordSession("facebeef-0000000001", 1730000000, 240, "/books/a.epub"));
  EXPECT_TRUE(fake_sd::disk.files.empty());
}
}  // namespace
