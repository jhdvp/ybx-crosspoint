#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "util/ReadingStats.h"

namespace {

using namespace readingstats;

// Mirror the paths in src/util/ReadingStats.cpp: the merge is tested through the
// public entry points, so the file names have to match what it writes.
constexpr const char* STATE_PATH = "/.crosspoint/reading-stats.bin";
constexpr const char* BOOKS_PATH = "/.crosspoint/reading-books.bin";
constexpr const char* BOOKS_PENDING = "/.crosspoint/reading-books.tmp";

constexpr const char* BOOK_A = "/books/ybx/alice.epub";
constexpr const char* BOOK_B = "/books/ybx/bob.epub";

void useZone(const char* zone) {
  setenv("TZ", zone, 1);
  tzset();
}

int64_t utcEpoch(const int year, const unsigned month, const unsigned day, const int hour, const int minute) {
  return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

// Writes a books table straight into the fake filesystem, bypassing the merge
// under test, to stand in for a table left by an earlier build or an interrupted
// replace.
void seedBooks(const std::vector<Book>& books) {
  uint8_t buffer[MAX_BOOKS_BYTES];
  const size_t length = serializeBooks(books.data(), static_cast<int>(books.size()), buffer, sizeof(buffer));
  ASSERT_GT(length, 0u);
  fake::put(BOOKS_PENDING, buffer, length);
}

Book makeBook(const char* path, const uint32_t seconds, const uint16_t sessions, const uint16_t lastDay) {
  Book book;
  copyText(book.path, sizeof(book.path), path);
  book.seconds = seconds;
  book.sessions = sessions;
  book.lastDay = lastDay;
  return book;
}

std::vector<Book> loadAllBooks() {
  std::vector<Book> books(MAX_BOOKS);
  int count = 0;
  loadBooks(books.data(), static_cast<int>(books.size()), count);
  books.resize(count);
  return books;
}

TEST(ReadingStatsFiles, FirstSessionCreatesBothFiles) {
  useZone("UTC");
  fake::reset();
  ASSERT_TRUE(recordSession("a1b2c3d4-1", utcEpoch(2026, 10, 28, 9, 0), 600, BOOK_A));

  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 600u);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.dayCount, 1);

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{1});  // the table is not created empty on first use
  EXPECT_STREQ(books[0].path, BOOK_A);
  EXPECT_EQ(books[0].seconds, 600u);
  EXPECT_EQ(books[0].sessions, 1);
  EXPECT_EQ(books[0].lastDay, daysFromCivil(2026, 10, 28));
  EXPECT_EQ(fake::files.count(BOOKS_PENDING), 0u);  // the temp copy was replaced
  EXPECT_GT(fake::files.count(STATE_PATH), 0u);
}

TEST(ReadingStatsFiles, SessionsOfOneBookAccumulateInOneRow) {
  useZone("UTC");
  fake::reset();
  ASSERT_TRUE(recordSession("a1b2c3d4-1", utcEpoch(2026, 10, 28, 9, 0), 600, BOOK_A));
  ASSERT_TRUE(recordSession("a1b2c3d4-2", utcEpoch(2026, 10, 28, 20, 0), 300, BOOK_A));
  ASSERT_TRUE(recordSession("a1b2c3d4-3", utcEpoch(2026, 10, 29, 9, 0), 120, BOOK_B));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{2});
  EXPECT_STREQ(books[0].path, BOOK_A);
  EXPECT_EQ(books[0].seconds, 900u);
  EXPECT_EQ(books[0].sessions, 2);
  EXPECT_EQ(books[0].lastDay, daysFromCivil(2026, 10, 28));
  EXPECT_STREQ(books[1].path, BOOK_B);
  EXPECT_EQ(books[1].seconds, 120u);

  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 1020u);
  EXPECT_EQ(state.totalSessions, 3u);
}

// A retry replays the id the checkpoint already carried, so it arrives as the
// immediately following recordSession call with the same id.
TEST(ReadingStatsFiles, ReplayedSessionIdIsCountedOnce) {
  useZone("UTC");
  fake::reset();
  const int64_t start = utcEpoch(2026, 10, 28, 9, 0);
  ASSERT_TRUE(recordSession("a1b2c3d4-7", start, 600, BOOK_A));
  ASSERT_TRUE(recordSession("a1b2c3d4-7", start, 600, BOOK_A));

  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 600u);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_TRUE(isRecorded(state, "a1b2c3d4-7"));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{1});
  EXPECT_EQ(books[0].seconds, 600u);
  EXPECT_EQ(books[0].sessions, 1);
}

TEST(ReadingStatsFiles, UntrustedClockStaysUndatedButKeepsTheBook) {
  useZone("UTC");
  fake::reset();
  ASSERT_TRUE(recordSession("a1b2c3d4-5", 0, 300, BOOK_A));

  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, 300u);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.undatedSeconds, 300u);
  EXPECT_EQ(state.undatedSessions, 1u);
  EXPECT_EQ(state.dayCount, 0);
  EXPECT_EQ(state.lastDay, 0);

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{1});
  EXPECT_EQ(books[0].seconds, 300u);
  EXPECT_EQ(books[0].lastDay, 0);
}

TEST(ReadingStatsFiles, CrossMidnightSessionSplitsDaysAndCountsOnce) {
  useZone("UTC");
  fake::reset();
  ASSERT_TRUE(recordSession("a1b2c3d4-9", utcEpoch(2026, 10, 28, 23, 30), 7200, BOOK_A));

  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.undatedSessions, 0u);
  ASSERT_EQ(state.dayCount, 2);
  EXPECT_EQ(state.days[0].ordinal, daysFromCivil(2026, 10, 28));
  EXPECT_EQ(state.days[0].seconds, 1800u);
  EXPECT_EQ(state.days[0].sessions, 1);
  EXPECT_EQ(state.days[1].ordinal, daysFromCivil(2026, 10, 29));
  EXPECT_EQ(state.days[1].seconds, 5400u);
  EXPECT_EQ(state.days[1].sessions, 0);
  EXPECT_EQ(state.activeDays, 2);
  EXPECT_EQ(state.lastDay, daysFromCivil(2026, 10, 29));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{1});
  EXPECT_EQ(books[0].seconds, 7200u);  // the whole session stays with the book
  EXPECT_EQ(books[0].sessions, 1);
}

// The read path already prefers the temp copy when the live table is gone; the
// merge must not rebuild from nothing and drop every book but the current one.
TEST(ReadingStatsFiles, TableLeftByAnInterruptedReplaceSurvivesTheNextSession) {
  useZone("UTC");
  fake::reset();
  seedBooks(
      {makeBook(BOOK_A, 600, 2, daysFromCivil(2026, 10, 27)), makeBook(BOOK_B, 240, 1, daysFromCivil(2026, 10, 28))});
  ASSERT_EQ(fake::files.count(BOOKS_PATH), 0u);

  const std::vector<Book> readable = loadAllBooks();
  ASSERT_EQ(readable.size(), size_t{2});

  ASSERT_TRUE(recordSession("a1b2c3d4-11", utcEpoch(2026, 10, 29, 8, 0), 100, BOOK_B));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{2});
  EXPECT_STREQ(books[0].path, BOOK_A);
  EXPECT_EQ(books[0].seconds, 600u);
  EXPECT_STREQ(books[1].path, BOOK_B);
  EXPECT_EQ(books[1].seconds, 340u);
  EXPECT_EQ(books[1].sessions, 2);
  EXPECT_EQ(books[1].lastDay, daysFromCivil(2026, 10, 29));
  EXPECT_EQ(fake::files.count(BOOKS_PENDING), 0u);
}

TEST(ReadingStatsFiles, UnusableLiveTableIsRebuiltFromTheCurrentSession) {
  useZone("UTC");
  fake::reset();
  seedBooks({makeBook(BOOK_A, 600, 1, daysFromCivil(2026, 10, 27))});
  fake::put(BOOKS_PATH, "junk", 4);

  ASSERT_TRUE(recordSession("a1b2c3d4-12", utcEpoch(2026, 10, 29, 8, 0), 100, BOOK_A));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{1});
  EXPECT_STREQ(books[0].path, BOOK_A);
  EXPECT_EQ(books[0].seconds, 100u);  // the unreadable live copy cannot be merged
}

// Drives the hazard through the code path: replaceFile() removes the live table
// and only then renames the temp copy onto it, so a failed rename leaves the
// newest table in the temp file with nothing beside it.
TEST(ReadingStatsFiles, ReplaceInterruptedAfterTheLiveTableIsRemovedKeepsHistory) {
  useZone("UTC");
  fake::reset();
  ASSERT_TRUE(recordSession("a1b2c3d4-13", utcEpoch(2026, 10, 28, 9, 0), 600, BOOK_A));

  fake::failRename = 1;  // the state replace goes through, the table replace does not
  EXPECT_TRUE(recordSession("a1b2c3d4-14", utcEpoch(2026, 10, 29, 9, 0), 60, BOOK_B));
  EXPECT_EQ(fake::files.count(BOOKS_PATH), 0u);

  ASSERT_EQ(loadAllBooks().size(), size_t{2});  // readable again through the temp copy

  ASSERT_TRUE(recordSession("a1b2c3d4-15", utcEpoch(2026, 10, 30, 9, 0), 40, BOOK_B));

  const std::vector<Book> books = loadAllBooks();
  ASSERT_EQ(books.size(), size_t{2});
  EXPECT_STREQ(books[0].path, BOOK_A);
  EXPECT_EQ(books[0].seconds, 600u);
  EXPECT_STREQ(books[1].path, BOOK_B);
  EXPECT_EQ(books[1].seconds, 100u);
  EXPECT_EQ(books[1].sessions, 2);
}

}  // namespace
