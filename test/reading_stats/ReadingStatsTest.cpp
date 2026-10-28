#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "util/ReadingStats.h"

namespace {

using namespace readingstats;

// localtime_r and mktime follow TZ, so tests pin it and never depend on the
// machine's zone.
void useZone(const char* zone) {
  setenv("TZ", zone, 1);
  tzset();
}

int64_t utcEpoch(const int year, const unsigned month, const unsigned day, const int hour, const int minute) {
  return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

TEST(ReadingStatsCalendar, CivilRoundTrip) {
  EXPECT_EQ(daysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(daysFromCivil(2000, 2, 29), 11016);
  EXPECT_EQ(daysFromCivil(2026, 10, 28), 20754);

  for (const int64_t ordinal : {int64_t{0}, int64_t{1}, int64_t{59}, int64_t{11016}, int64_t{20754}, int64_t{65535}}) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(ordinal, year, month, day);
    EXPECT_EQ(daysFromCivil(year, month, day), ordinal) << ordinal;
  }
}

TEST(ReadingStatsCalendar, UnsetClockIsUndated) {
  EXPECT_EQ(localDayOrdinal(0), 0);
  EXPECT_EQ(localDayOrdinal(-1), 0);
  useZone("UTC");
  EXPECT_EQ(localDayOrdinal(utcEpoch(2026, 10, 28, 12, 0)), daysFromCivil(2026, 10, 28));
}

TEST(ReadingStatsSplit, WithinOneDay) {
  useZone("UTC");
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 0;
  ASSERT_TRUE(splitSession(utcEpoch(2026, 10, 28, 10, 0), 600, segments, MAX_SESSION_SEGMENTS, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(segments[0].day, daysFromCivil(2026, 10, 28));
  EXPECT_EQ(segments[0].seconds, 600u);
}

TEST(ReadingStatsSplit, AcrossMidnightSplitsAtLocalMidnight) {
  useZone("UTC");
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 0;
  ASSERT_TRUE(splitSession(utcEpoch(2026, 10, 28, 23, 30), 7200, segments, MAX_SESSION_SEGMENTS, count));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(segments[0].day, daysFromCivil(2026, 10, 28));
  EXPECT_EQ(segments[0].seconds, 1800u);
  EXPECT_EQ(segments[1].day, daysFromCivil(2026, 10, 29));
  EXPECT_EQ(segments[1].seconds, 5400u);
}

TEST(ReadingStatsSplit, ManyDaysKeepTheSum) {
  useZone("UTC");
  constexpr uint32_t THREE_DAYS_TWO_HOURS = 3 * 86400 + 2 * 3600;
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 0;
  ASSERT_TRUE(splitSession(utcEpoch(2026, 10, 28, 22, 0), THREE_DAYS_TWO_HOURS, segments, MAX_SESSION_SEGMENTS, count));
  ASSERT_EQ(count, 4);
  uint64_t sum = 0;
  for (int i = 0; i < count; i++) sum += segments[i].seconds;
  EXPECT_EQ(sum, THREE_DAYS_TWO_HOURS);
  EXPECT_EQ(segments[0].seconds, 7200u);
  EXPECT_EQ(segments[3].seconds, 86400u);
}

TEST(ReadingStatsSplit, CapacityFoldsRemainderIntoLastSegment) {
  useZone("UTC");
  SessionSegment segments[2];
  int count = 0;
  ASSERT_TRUE(splitSession(utcEpoch(2026, 10, 28, 22, 0), 5 * 86400, segments, 2, count));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(uint64_t{segments[0].seconds} + segments[1].seconds, uint64_t{5 * 86400});
}

TEST(ReadingStatsSplit, UnusableStartTime) {
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 3;
  EXPECT_FALSE(splitSession(0, 600, segments, MAX_SESSION_SEGMENTS, count));
  EXPECT_FALSE(splitSession(-100, 600, segments, MAX_SESSION_SEGMENTS, count));
}

TEST(ReadingStatsSplit, SpringForwardDayKeepsTheSum) {
  useZone("America/New_York");
  const int64_t start = utcEpoch(2026, 3, 8, 5, 30);  // 2026-03-08 00:30 EST, a 23-hour day
  std::tm local{};
  const std::time_t value = static_cast<std::time_t>(start);
  ASSERT_NE(localtime_r(&value, &local), nullptr);
  if (local.tm_hour != 0 || local.tm_mday != 8) {
    GTEST_SKIP() << "tzdata for America/New_York is unavailable";
  }
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 0;
  ASSERT_TRUE(splitSession(start, 24 * 3600, segments, MAX_SESSION_SEGMENTS, count));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(segments[0].day, daysFromCivil(2026, 3, 8));
  EXPECT_EQ(segments[0].seconds, 22 * 3600 + 1800);  // the day is one hour short
  EXPECT_EQ(segments[1].seconds, 3600 + 1800);
  EXPECT_EQ(segments[1].day, daysFromCivil(2026, 3, 9));
}

TEST(ReadingStatsDays, RingEvictsOldestAndStopsBeingAWindow) {
  State state;
  for (int i = 1; i <= MAX_DAYS; i++) {
    addDaySeconds(state, static_cast<uint16_t>(100 + i), 60, 1);
  }
  ASSERT_EQ(state.dayCount, MAX_DAYS);
  EXPECT_EQ(state.activeDays, MAX_DAYS);
  EXPECT_EQ(state.days[0].ordinal, 101);

  addDaySeconds(state, 165, 60, 1);
  EXPECT_EQ(state.dayCount, MAX_DAYS);
  EXPECT_EQ(state.days[0].ordinal, 102);
  EXPECT_EQ(state.days[MAX_DAYS - 1].ordinal, 165);
  EXPECT_EQ(state.activeDays, MAX_DAYS + 1);

  // A day older than the ring still counts as active but has no slot.
  addDaySeconds(state, 50, 60, 1);
  EXPECT_EQ(state.dayCount, MAX_DAYS);
  EXPECT_EQ(state.activeDays, MAX_DAYS + 2);
  EXPECT_EQ(state.days[0].ordinal, 102);

  addDaySeconds(state, 165, 30, 2);
  EXPECT_EQ(state.days[MAX_DAYS - 1].seconds, 90u);
  EXPECT_EQ(state.days[MAX_DAYS - 1].sessions, 3);
}

TEST(ReadingStatsDays, InsertKeepsAscendingOrder) {
  State state;
  addDaySeconds(state, 20, 60, 1);
  addDaySeconds(state, 10, 60, 1);
  addDaySeconds(state, 15, 60, 1);
  ASSERT_EQ(state.dayCount, 3);
  EXPECT_EQ(state.days[0].ordinal, 10);
  EXPECT_EQ(state.days[1].ordinal, 15);
  EXPECT_EQ(state.days[2].ordinal, 20);
  EXPECT_EQ(state.activeDays, 3);
}

TEST(ReadingStatsSessions, DatedSessionUpdatesTotalsAndDay) {
  useZone("UTC");
  State state;
  const int64_t start = utcEpoch(2026, 10, 28, 10, 0);
  EXPECT_EQ(addSession(state, start, 600), daysFromCivil(2026, 10, 28));
  EXPECT_EQ(state.totalSeconds, 600u);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.undatedSeconds, 0u);
  EXPECT_EQ(state.lastDay, daysFromCivil(2026, 10, 28));
  EXPECT_EQ(state.lastSessionEpoch, static_cast<uint32_t>(start));
  ASSERT_EQ(state.dayCount, 1);
  EXPECT_EQ(state.days[0].seconds, 600u);
  EXPECT_EQ(state.days[0].sessions, 1);
}

TEST(ReadingStatsSessions, UndatedSessionStaysInTotalsOnly) {
  State state;
  EXPECT_EQ(addSession(state, 0, 300), 0);
  EXPECT_EQ(state.totalSeconds, 300u);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.undatedSeconds, 300u);
  EXPECT_EQ(state.undatedSessions, 1u);
  EXPECT_EQ(state.dayCount, 0);
  EXPECT_EQ(state.lastDay, 0);
  EXPECT_EQ(state.lastSessionEpoch, 0u);
}

TEST(ReadingStatsSessions, CrossMidnightCountsOneSessionOnItsFirstDay) {
  useZone("UTC");
  State state;
  addSession(state, utcEpoch(2026, 10, 28, 23, 30), 7200);
  ASSERT_EQ(state.dayCount, 2);
  EXPECT_EQ(state.days[0].sessions, 1);
  EXPECT_EQ(state.days[1].sessions, 0);
  EXPECT_EQ(state.totalSessions, 1u);
  EXPECT_EQ(state.lastDay, daysFromCivil(2026, 10, 29));
}

TEST(ReadingStatsSessions, ZeroDurationIsIgnored) {
  State state;
  addSession(state, utcEpoch(2026, 10, 28, 10, 0), 0);
  EXPECT_EQ(state.totalSeconds, 0u);
  EXPECT_EQ(state.dayCount, 0);
}

TEST(ReadingStatsSessions, SumWindowIsInclusive) {
  State state;
  addDaySeconds(state, 10, 100, 1);
  addDaySeconds(state, 11, 200, 1);
  addDaySeconds(state, 12, 400, 1);
  addDaySeconds(state, 20, 800, 1);
  EXPECT_EQ(sumDays(state, 10, 12), 700u);
  EXPECT_EQ(sumDays(state, 12, 20), 1200u);
  EXPECT_EQ(sumDays(state, 13, 19), 0u);
}

TEST(ReadingStatsSessions, StreakEndsTodayOrYesterday) {
  State state;
  bool clipped = true;
  EXPECT_EQ(streakDays(state, 102, clipped), 0);
  EXPECT_FALSE(clipped);

  addDaySeconds(state, 100, 60, 1);
  addDaySeconds(state, 101, 60, 1);
  addDaySeconds(state, 102, 60, 1);
  EXPECT_EQ(streakDays(state, 102, clipped), 3);
  EXPECT_FALSE(clipped);
  EXPECT_EQ(streakDays(state, 103, clipped), 3);
  EXPECT_EQ(streakDays(state, 104, clipped), 0);
  EXPECT_EQ(streakDays(state, 0, clipped), 0);

  addDaySeconds(state, 104, 60, 1);
  EXPECT_EQ(streakDays(state, 104, clipped), 1);
}

TEST(ReadingStatsSessions, StreakReportsClippingAtRingBoundary) {
  State state;
  for (int i = 0; i < MAX_DAYS; i++) {
    addDaySeconds(state, static_cast<uint16_t>(100 + i), 60, 1);
  }
  bool clipped = false;
  EXPECT_EQ(streakDays(state, 163, clipped), MAX_DAYS);
  EXPECT_TRUE(clipped);
}

TEST(ReadingStatsSessions, DedupeId) {
  State state;
  EXPECT_FALSE(isRecorded(state, nullptr));
  EXPECT_FALSE(isRecorded(state, ""));
  noteSession(state, "a1b2c3d4-1730000000", 1730000000);
  EXPECT_TRUE(isRecorded(state, "a1b2c3d4-1730000000"));
  EXPECT_FALSE(isRecorded(state, "a1b2c3d4-1730000001"));
  EXPECT_EQ(state.lastSessionEpoch, 1730000000u);

  // The field keeps 19 characters; the real reader-session id format
  // <8 hex>-<uptime ms> is exactly 19 characters and is stored whole.
  noteSession(state, "0123456789abcdef-1234567890", 0);
  EXPECT_TRUE(isRecorded(state, "0123456789abcdef-12"));
  EXPECT_EQ(std::strlen(state.lastSessionId), size_t{MAX_ID_LENGTH});
}

TEST(ReadingStatsText, CopyTextKeepsUtf8Intact) {
  char buffer[8];
  copyText(buffer, sizeof(buffer), "Привет");
  EXPECT_EQ(std::string(buffer), "При");

  copyText(buffer, sizeof(buffer), nullptr);
  EXPECT_STREQ(buffer, "");

  char exact[13];
  copyText(exact, sizeof(exact), "Привет");
  EXPECT_STREQ(exact, "Привет");
}

TEST(ReadingStatsBooks, AccumulatesAndEvictsLeastRecentlyRead) {
  std::array<Book, MAX_BOOKS> books{};
  int count = 0;
  addBook(books.data(), count, "/books/a.epub", 600, 1, 10);
  addBook(books.data(), count, "/books/a.epub", 300, 1, 12);
  addBook(books.data(), count, "/books/b.epub", 60, 0, 5);
  ASSERT_EQ(count, 2);
  EXPECT_EQ(books[0].seconds, 900u);
  EXPECT_EQ(books[0].sessions, 2);
  EXPECT_EQ(books[0].lastDay, 12);
  EXPECT_EQ(books[1].seconds, 60u);
  EXPECT_EQ(books[1].sessions, 0);

  addBook(books.data(), count, "/books/a.epub", 60, 0, 8);
  EXPECT_EQ(books[0].lastDay, 12);  // an older session must not rewind it

  while (count < MAX_BOOKS) {
    addBook(books.data(), count, "/books/fill" + std::to_string(count) + ".epub", 60, 1,
            static_cast<uint16_t>(20 + count));
  }
  ASSERT_EQ(count, MAX_BOOKS);
  addBook(books.data(), count, "/books/new.epub", 60, 1, 90);
  ASSERT_EQ(count, MAX_BOOKS);
  bool foundNew = false;
  bool foundOldest = false;
  for (int i = 0; i < count; i++) {
    foundNew = foundNew || std::strcmp(books[i].path, "/books/new.epub") == 0;
    foundOldest = foundOldest || std::strcmp(books[i].path, "/books/b.epub") == 0;
  }
  EXPECT_TRUE(foundNew);
  EXPECT_FALSE(foundOldest);
}

TEST(ReadingStatsBooks, EmptyPathAndZeroSecondsAreIgnored) {
  std::array<Book, MAX_BOOKS> books{};
  int count = 0;
  addBook(books.data(), count, "", 600, 1, 10);
  addBook(books.data(), count, "/books/a.epub", 0, 0, 10);
  addBook(nullptr, count, "/books/a.epub", 600, 1, 10);
  EXPECT_EQ(count, 0);
}

TEST(ReadingStatsBooks, LongPathIsTruncatedWithoutSplittingUtf8) {
  std::array<Book, MAX_BOOKS> books{};
  int count = 0;
  std::string path = "/books/";
  while (path.size() < MAX_BOOK_PATH + 20) path += "п";
  addBook(books.data(), count, path, 60, 1, 10);
  addBook(books.data(), count, path, 30, 1, 10);
  ASSERT_EQ(count, 1);
  EXPECT_EQ(books[0].seconds, 90u);
  EXPECT_LT(std::strlen(books[0].path), MAX_BOOK_PATH);
  EXPECT_EQ(path.compare(0, std::strlen(books[0].path), books[0].path), 0);
}

TEST(ReadingStatsState, RoundTripPreservesEverything) {
  useZone("UTC");
  State state;
  addSession(state, utcEpoch(2026, 10, 27, 23, 30), 7200);
  addSession(state, utcEpoch(2026, 10, 28, 10, 0), 600);
  addSession(state, 0, 120);
  noteSession(state, "a1b2c3d4-1793230200", utcEpoch(2026, 10, 28, 10, 0));

  std::array<uint8_t, MAX_STATE_BYTES> buffer{};
  const size_t length = serializeState(state, buffer.data(), buffer.size());
  ASSERT_EQ(length, STATE_HEADER_BYTES + 2 * DAY_RECORD_BYTES);

  State parsed;
  ASSERT_TRUE(parseState(buffer.data(), length, parsed));
  EXPECT_EQ(parsed.totalSeconds, state.totalSeconds);
  EXPECT_EQ(parsed.totalSessions, state.totalSessions);
  EXPECT_EQ(parsed.undatedSeconds, state.undatedSeconds);
  EXPECT_EQ(parsed.undatedSessions, state.undatedSessions);
  EXPECT_EQ(parsed.activeDays, state.activeDays);
  EXPECT_EQ(parsed.lastDay, state.lastDay);
  EXPECT_EQ(parsed.lastSessionEpoch, state.lastSessionEpoch);
  EXPECT_STREQ(parsed.lastSessionId, state.lastSessionId);
  ASSERT_EQ(parsed.dayCount, state.dayCount);
  for (int i = 0; i < parsed.dayCount; i++) {
    EXPECT_EQ(parsed.days[i].ordinal, state.days[i].ordinal);
    EXPECT_EQ(parsed.days[i].seconds, state.days[i].seconds);
    EXPECT_EQ(parsed.days[i].sessions, state.days[i].sessions);
  }
}

TEST(ReadingStatsState, RejectsCorruptAndShortBuffers) {
  State state;
  addDaySeconds(state, 10, 100, 1);
  addDaySeconds(state, 11, 200, 2);

  std::array<uint8_t, MAX_STATE_BYTES> buffer{};
  const size_t length = serializeState(state, buffer.data(), buffer.size());
  ASSERT_GT(length, 0u);

  State parsed;
  EXPECT_FALSE(parseState(buffer.data(), STATE_HEADER_BYTES - 1, parsed));
  EXPECT_FALSE(parseState(buffer.data(), length - 1, parsed));
  EXPECT_FALSE(parseState(nullptr, length, parsed));

  auto corrupt = buffer;
  corrupt[0] = 'X';
  EXPECT_FALSE(parseState(corrupt.data(), length, parsed));

  corrupt = buffer;
  corrupt[4] = 2;
  EXPECT_FALSE(parseState(corrupt.data(), length, parsed));

  corrupt = buffer;
  corrupt[5] = MAX_DAYS + 1;
  EXPECT_FALSE(parseState(corrupt.data(), length, parsed));

  corrupt = buffer;
  std::array<uint8_t, DAY_RECORD_BYTES> first{};
  std::memcpy(first.data(), corrupt.data() + STATE_HEADER_BYTES, first.size());
  std::memcpy(corrupt.data() + STATE_HEADER_BYTES, corrupt.data() + STATE_HEADER_BYTES + DAY_RECORD_BYTES,
              DAY_RECORD_BYTES);
  std::memcpy(corrupt.data() + STATE_HEADER_BYTES + DAY_RECORD_BYTES, first.data(), first.size());
  EXPECT_FALSE(parseState(corrupt.data(), length, parsed));
}

TEST(ReadingStatsState, ZeroSecondDayStaysActiveAfterParse) {
  State state;
  state.dayCount = 1;
  state.days[0] = Day{42, 0, 0};
  std::array<uint8_t, MAX_STATE_BYTES> buffer{};
  const size_t length = serializeState(state, buffer.data(), buffer.size());
  ASSERT_GT(length, 0u);
  State parsed;
  ASSERT_TRUE(parseState(buffer.data(), length, parsed));
  EXPECT_EQ(parsed.days[0].seconds, 1u);
}

TEST(ReadingStatsState, BufferTooSmall) {
  State state;
  addDaySeconds(state, 10, 100, 1);
  std::array<uint8_t, STATE_HEADER_BYTES> buffer{};
  EXPECT_EQ(serializeState(state, buffer.data(), buffer.size()), 0u);
}

TEST(ReadingStatsBooksFile, RoundTrip) {
  std::array<Book, MAX_BOOKS> table{};
  int count = 0;
  addBook(table.data(), count, "/books/a.epub", 600, 2, 10);
  addBook(table.data(), count, "/books/b.epub", 60, 1, 11);

  std::array<uint8_t, MAX_BOOKS_BYTES> buffer{};
  const size_t length = serializeBooks(table.data(), count, buffer.data(), buffer.size());
  ASSERT_EQ(length, BOOKS_HEADER_BYTES + 2 * BOOK_RECORD_BYTES);

  std::array<Book, MAX_BOOKS> parsed{};
  int parsedCount = 0;
  ASSERT_TRUE(parseBooks(buffer.data(), length, parsed.data(), MAX_BOOKS, parsedCount));
  ASSERT_EQ(parsedCount, 2);
  EXPECT_STREQ(parsed[0].path, "/books/a.epub");
  EXPECT_EQ(parsed[0].seconds, 600u);
  EXPECT_EQ(parsed[0].sessions, 2);
  EXPECT_EQ(parsed[1].lastDay, 11);
}

TEST(ReadingStatsBooksFile, RejectsCorruptFiles) {
  std::array<Book, 2> table{};
  int count = 0;
  addBook(table.data(), count, "/books/a.epub", 600, 1, 10);

  std::array<uint8_t, MAX_BOOKS_BYTES> buffer{};
  const size_t length = serializeBooks(table.data(), count, buffer.data(), buffer.size());
  ASSERT_GT(length, 0u);

  std::array<Book, MAX_BOOKS> parsed{};
  int parsedCount = 0;
  EXPECT_FALSE(parseBooks(buffer.data(), BOOKS_HEADER_BYTES - 1, parsed.data(), MAX_BOOKS, parsedCount));
  EXPECT_FALSE(parseBooks(buffer.data(), length - 1, parsed.data(), MAX_BOOKS, parsedCount));
  EXPECT_FALSE(parseBooks(nullptr, length, parsed.data(), MAX_BOOKS, parsedCount));

  auto corrupt = buffer;
  corrupt[0] = 'X';
  EXPECT_FALSE(parseBooks(corrupt.data(), length, parsed.data(), MAX_BOOKS, parsedCount));

  corrupt = buffer;
  corrupt[4] = 2;
  EXPECT_FALSE(parseBooks(corrupt.data(), length, parsed.data(), MAX_BOOKS, parsedCount));

  corrupt = buffer;
  corrupt[5] = MAX_BOOKS + 1;
  EXPECT_FALSE(parseBooks(corrupt.data(), length, parsed.data(), MAX_BOOKS, parsedCount));
}

TEST(ReadingStatsBooksFile, SaturationNeverWraps) {
  EXPECT_EQ(saturatingAdd(UINT32_MAX - 5, 10), UINT32_MAX);
  EXPECT_EQ(saturatingAdd(0, 0), 0u);
  EXPECT_EQ(saturatingAdd16(UINT16_MAX - 5, 10), UINT16_MAX);

  std::array<Book, MAX_BOOKS> table{};
  int count = 0;
  addBook(table.data(), count, "/books/a.epub", UINT32_MAX - 5, 1, 10);
  addBook(table.data(), count, "/books/a.epub", 10, 1, 10);
  EXPECT_EQ(table[0].seconds, UINT32_MAX);
  EXPECT_EQ(table[0].sessions, 2);
}

TEST(ReadingStatsFiles, SizeConstantsMatchTheSerializedLayout) {
  State state;
  state.dayCount = MAX_DAYS;
  for (int i = 0; i < MAX_DAYS; i++) state.days[i] = Day{static_cast<uint16_t>(i + 1), 60, 1};
  std::array<uint8_t, MAX_STATE_BYTES + 1> stateBuffer{};
  EXPECT_EQ(serializeState(state, stateBuffer.data(), MAX_STATE_BYTES), MAX_STATE_BYTES);

  std::array<Book, MAX_BOOKS> table{};
  int count = 0;
  for (int i = 0; i < MAX_BOOKS; i++) {
    addBook(table.data(), count, "/books/" + std::to_string(i) + ".epub", 60, 1, static_cast<uint16_t>(i + 1));
  }
  std::array<uint8_t, MAX_BOOKS_BYTES + 1> booksBuffer{};
  EXPECT_EQ(serializeBooks(table.data(), count, booksBuffer.data(), MAX_BOOKS_BYTES), MAX_BOOKS_BYTES);
}

}  // namespace
