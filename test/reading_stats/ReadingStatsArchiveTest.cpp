#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "util/ReadingStatsArchive.h"

namespace {
using namespace readingstats;
using namespace readingstats::archive;

// Buffer-only fake of the archive IO contract, with fault-injection counters
// mirroring the setup used for the snapshot storage tests.
struct FakeIO {
  std::map<std::string, std::vector<uint8_t>> files;
  int failRead = -1;
  int failWrite = -1;
  int readCount = 0;
  int writeCalls = 0;

  void reset() {
    files.clear();
    failRead = failWrite = -1;
    readCount = writeCalls = 0;
  }
  bool failOnce(int* countdown) {
    if (*countdown < 0) return false;
    if (*countdown == 0) {
      *countdown = -1;
      return true;
    }
    --*countdown;
    return false;
  }
  bool exists(const std::string& path) const { return files.count(path) != 0; }
  bool read(const std::string& path, uint8_t* dst, const size_t cap, size_t& size) {
    ++readCount;
    if (failOnce(&failRead)) return false;
    size = 0;
    if (!exists(path)) return false;
    const auto& bytes = files.at(path);
    if (bytes.size() > cap) return false;
    std::copy_n(bytes.data(), bytes.size(), dst);
    size = bytes.size();
    return true;
  }
  bool write(const std::string& path, const uint8_t* src, const size_t size) {
    ++writeCalls;
    if (failOnce(&failWrite)) return false;
    files[path].assign(src, src + size);
    return true;
  }
  bool put(const std::string& path, const std::string& bytes) {
    files[path].assign(bytes.begin(), bytes.end());
    return true;
  }
  std::string get(const std::string& path) const {
    const auto& bytes = files.at(path);
    return {bytes.begin(), bytes.end()};
  }
};

uint16_t ordinalOf(const int year, const unsigned month, const unsigned day) {
  return static_cast<uint16_t>(daysFromCivil(year, month, day));
}

std::string nameOf(const int year, const unsigned month, const char slot) {
  char stamp[16] = {};
  snprintf(stamp, sizeof(stamp), "/%04u-%02u.%c.bin", year, month, slot);
  return std::string(DIRECTORY) + stamp;
}

struct Fixture : testing::Test {
  FakeIO io;
  void SetUp() override { io.reset(); }
};

}  // namespace

TEST_F(Fixture, MonthNamingMatchesTheCalendar) {
  char path[48];
  monthPath(daysFromCivil(2026, 11, 1), 0, path);
  EXPECT_EQ(path, std::string(DIRECTORY) + "/2026-11.a.bin");
  monthPath(daysFromCivil(2026, 11, 1), 1, path);
  EXPECT_EQ(path, std::string(DIRECTORY) + "/2026-11.b.bin");
  monthPath(daysFromCivil(2028, 2, 1), 0, path);  // leap February
  EXPECT_EQ(path, std::string(DIRECTORY) + "/2028-02.a.bin");
  monthPath(daysFromCivil(2000, 1, 1), 1, path);
  EXPECT_EQ(path, std::string(DIRECTORY) + "/2000-01.b.bin");

  uint32_t firstDay = 0;
  uint8_t days = 0;
  ASSERT_TRUE(monthRange(ordinalOf(2026, 11, 7), firstDay, days));
  EXPECT_EQ(firstDay, daysFromCivil(2026, 11, 1));
  EXPECT_EQ(days, 30);
  ASSERT_TRUE(monthRange(ordinalOf(2024, 2, 29), firstDay, days));
  EXPECT_EQ(firstDay, daysFromCivil(2024, 2, 1));
  EXPECT_EQ(days, 29);
}

TEST_F(Fixture, FirstAppendKeepsAnEmptyRecoveryCopyBeforeTheDataCopy) {
  const uint16_t day = ordinalOf(2026, 11, 7);
  EXPECT_TRUE(addSeconds(io, day, 10));
  EXPECT_EQ(io.files.count(nameOf(2026, 11, 'a')), 1u);
  EXPECT_EQ(io.files.count(nameOf(2026, 11, 'b')), 1u);
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 10u);
}

TEST_F(Fixture, SameDayAppendsAccumulateAndMonthsStaySeparate) {
  const uint16_t lateNovember = ordinalOf(2026, 11, 30);
  const uint16_t earlyDecember = ordinalOf(2026, 12, 1);
  EXPECT_TRUE(addSeconds(io, lateNovember, 100));
  EXPECT_TRUE(addSeconds(io, lateNovember, 20));
  EXPECT_TRUE(addSeconds(io, earlyDecember, 5));
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, lateNovember, seconds));
  EXPECT_EQ(seconds, 120u);
  EXPECT_TRUE(daySeconds(io, earlyDecember, seconds));
  EXPECT_EQ(seconds, 5u);
  EXPECT_EQ(io.files.count(nameOf(2026, 11, 'a')), 1u);
  EXPECT_EQ(io.files.count(nameOf(2026, 12, 'a')), 1u);
}

TEST_F(Fixture, MissingMonthReadsAsZeroWithoutError) {
  uint32_t seconds = 7;
  EXPECT_TRUE(daySeconds(io, ordinalOf(2026, 11, 7), seconds));
  EXPECT_EQ(seconds, 0u);
}

TEST_F(Fixture, CorruptNewestCopyFallsBackToTheOlderGeneration) {
  const uint16_t day = ordinalOf(2026, 11, 7);
  ASSERT_TRUE(addSeconds(io, day, 10));
  ASSERT_TRUE(addSeconds(io, day, 15));  // data now lives in slot b, generation 2

  // Corrupt the newest copy without changing its size or header day.
  ++io.files[nameOf(2026, 11, 'b')][30];
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 10u);  // the committed generation 1 copy

  // A retry merges with what the fallback copy holds.
  EXPECT_TRUE(addSeconds(io, day, 3));
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 13u);
}

TEST_F(Fixture, FailedWriteKeepsThePreviousCopy) {
  io.failWrite = 0;  // the very next write fails
  const uint16_t day = ordinalOf(2026, 11, 7);
  EXPECT_FALSE(addSeconds(io, day, 10));
  EXPECT_TRUE(io.files.empty());

  ASSERT_TRUE(addSeconds(io, day, 10));
  io.failWrite = 0;
  EXPECT_FALSE(addSeconds(io, day, 15));
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 10u);
}

TEST_F(Fixture, LoneTornCopyIsNeverReplaced) {
  const uint16_t day = ordinalOf(2026, 11, 7);
  ASSERT_TRUE(io.put(nameOf(2026, 11, 'a'), std::string("\x01torn", 5)));
  EXPECT_FALSE(addSeconds(io, day, 10));
  EXPECT_EQ(io.get(nameOf(2026, 11, 'a')), std::string("\x01torn", 5));
  uint32_t seconds = 0;
  EXPECT_FALSE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 0u);
}

TEST_F(Fixture, BothInvalidCopiesAreRetained) {
  // Slot b torn alone can be reinitialized (see LoneTornSlotB); a pair of
  // damage refuses writes instead of guessing which copy held history.
  const uint16_t day = ordinalOf(2026, 11, 7);
  ASSERT_TRUE(io.put(nameOf(2026, 11, 'a'), std::string("\x01torn", 5)));
  ASSERT_TRUE(io.put(nameOf(2026, 11, 'b'), std::string("\x01torn", 5)));
  EXPECT_FALSE(addSeconds(io, day, 10));
  EXPECT_EQ(io.get(nameOf(2026, 11, 'a')), std::string("\x01torn", 5));
  EXPECT_EQ(io.get(nameOf(2026, 11, 'b')), std::string("\x01torn", 5));
}

TEST_F(Fixture, LoneTornSlotBStartsAFreshMonth) {
  // The empty recovery copy is written before any data copy, so without a
  // valid sibling a torn slot b has never held committed history.
  const uint16_t day = ordinalOf(2026, 11, 7);
  ASSERT_TRUE(io.put(nameOf(2026, 11, 'b'), std::string("\x01torn", 5)));
  EXPECT_TRUE(addSeconds(io, day, 10));
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, 10u);
}

TEST_F(Fixture, NewerFormatCopiesAreRefused) {
  const uint16_t day = ordinalOf(2026, 11, 7);
  ASSERT_TRUE(addSeconds(io, day, 10));
  io.files[nameOf(2026, 11, 'a')].at(0) = 3;  // unknown version
  EXPECT_FALSE(addSeconds(io, day, 5));
  EXPECT_EQ(io.get(nameOf(2026, 11, 'a')).at(0), 3);
}

TEST_F(Fixture, SecondsBeyondTheMonthLengthMakeTheCopyInvalid) {
  Month crafted;
  crafted.seconds[30] = 7;  // November has 30 days
  uint8_t raw[MONTH_BYTES];
  serializeMonth(crafted, daysFromCivil(2026, 11, 1), raw);
  const std::string bytes(reinterpret_cast<const char*>(raw), sizeof(raw));
  ASSERT_TRUE(io.put(nameOf(2026, 11, 'a'), bytes));
  EXPECT_FALSE(addSeconds(io, ordinalOf(2026, 11, 7), 5));
  uint32_t seconds = 0;
  EXPECT_FALSE(daySeconds(io, ordinalOf(2026, 11, 7), seconds));
  EXPECT_EQ(seconds, 0u);
}

TEST_F(Fixture, SizesAndSaturationStayBounded) {
  static_assert(MONTH_BYTES == 140);
  static_assert(sizeof(Month) <= 256);
  const uint16_t day = ordinalOf(2026, 11, 7);
  EXPECT_TRUE(addSeconds(io, day, 60));
  EXPECT_EQ(io.writeCalls, 2u);  // empty recovery copy, then data
  // A later day exceeding uint32 sums is capped, not wrapped.
  EXPECT_TRUE(addSeconds(io, day, UINT32_MAX / 2));
  EXPECT_TRUE(addSeconds(io, day, UINT32_MAX));
  EXPECT_EQ(io.writeCalls, 4u);
  uint32_t seconds = 0;
  EXPECT_TRUE(daySeconds(io, day, seconds));
  EXPECT_EQ(seconds, UINT32_MAX);

  // Undated and zero-second calls are no-ops, never archive writes.
  EXPECT_TRUE(addSeconds(io, 0, 60));
  EXPECT_TRUE(addSeconds(io, day, 0));
  EXPECT_EQ(io.writeCalls, 4u);
}
