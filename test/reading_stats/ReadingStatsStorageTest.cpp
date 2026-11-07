#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "HalStorage.h"
#include "util/ReadingStats.h"
#include "util/ReadingStatsArchive.h"
#include "util/ReadingStatsSnapshot.h"

namespace {
using namespace readingstats;
struct Fixture : testing::Test {
  void SetUp() override { fake_sd::disk.reset(); }
};

// Test-local adapter matching the buffer IO contract for the archive.
struct SdFakeArchiveIO {
  bool exists(const char* path) const { return fake_sd::disk.files.count(path) != 0; }
  bool read(const char* path, uint8_t* dst, const size_t cap, size_t& size) {
    size = 0;
    if (!exists(path)) return false;
    const std::string& data = fake_sd::disk.files.at(path);
    if (data.size() > cap) return false;
    std::copy_n(data.data(), data.size(), dst);
    size = data.size();
    return true;
  }
  bool write(const char* path, const uint8_t* src, const size_t size) {
    fake_sd::disk.files[path].assign(reinterpret_cast<const char*>(src), size);
    return true;
  }
};

void useZone(const char* zone) {
  setenv("TZ", zone, 1);
  tzset();
}
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

// Year-long daily reading evicts days from the MAX_DAYS ring; those seconds
// must reach the monthly archive under /.crosspoint/daily_reading. The chart
// keeps reading the ring and is unchanged.
TEST_F(Fixture, RingEvictionsReachTheMonthlyHistoryArchive) {
  useZone("UTC");
  const int64_t base = daysFromCivil(2026, 11, 1);
  char id[20] = {};
  for (int i = 0; i < MAX_DAYS; ++i) {
    snprintf(id, sizeof(id), "facebeef-%010d", i);
    // One new day per session, filling the ring from oldest to newest.
    ASSERT_TRUE(recordSession(id, (base - (MAX_DAYS - i)) * 86400 + 3600, 60, "")) << i;
  }
  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, static_cast<uint32_t>(MAX_DAYS) * 60);
  EXPECT_EQ(state.dayCount, MAX_DAYS);
  // Filling the ring alone never touches the archive (the snapshot files do).
  for (const auto& [path, bytes] : fake_sd::disk.files) {
    (void)bytes;
    EXPECT_TRUE(path.rfind(archive::DIRECTORY, 0) != 0) << path;
  }

  // The next day evicts the oldest ring entry into the archive.
  ASSERT_TRUE(recordSession("facebeef-0000000064", (base + 1) * 86400 + 3600, 120, ""));
  state = State{};
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, static_cast<uint32_t>(MAX_DAYS) * 60 + 120);
  EXPECT_EQ(state.dayCount, MAX_DAYS);
  EXPECT_EQ(state.days[0].ordinal, base - MAX_DAYS + 1);

  // The archived copy holds the evicted day exactly.
  SdFakeArchiveIO archiveIo;
  uint32_t seconds = 0;
  ASSERT_TRUE(archive::daySeconds(archiveIo, static_cast<uint16_t>(base - MAX_DAYS), seconds));
  EXPECT_EQ(seconds, 60u);
}

TEST_F(Fixture, FailedArchiveWriteNeverFailsTheSessionRecord) {
  useZone("UTC");
  const int64_t base = daysFromCivil(2026, 11, 1);
  char id[20] = {};
  for (int i = 0; i < MAX_DAYS; ++i) {
    snprintf(id, sizeof(id), "facebeef-%010d", i);
    ASSERT_TRUE(recordSession(id, (base - (MAX_DAYS - i)) * 86400 + 3600, 60, "")) << i;
  }
  // The evicted month is 2026-08; fail its first archive write.
  fake_sd::disk.failWritePath = std::string(archive::DIRECTORY) + "/2026-08.b.bin";
  ASSERT_TRUE(recordSession("facebeef-0000000064", (base + 1) * 86400 + 3600, 120, ""));
  fake_sd::disk.failWritePath.clear();
  State state;
  ASSERT_TRUE(loadState(state));
  EXPECT_EQ(state.totalSeconds, static_cast<uint32_t>(MAX_DAYS) * 60 + 120);
  EXPECT_EQ(state.dayCount, MAX_DAYS);
  // The session itself is committed; the archive write failed and left an
  // empty touched slot only.
  EXPECT_EQ(fake_sd::disk.files.count(std::string(archive::DIRECTORY) + "/2026-08.a.bin"), 0u);
  EXPECT_EQ(fake_sd::disk.files.at(std::string(archive::DIRECTORY) + "/2026-08.b.bin"), std::string());
}

TEST_F(Fixture, ReplayAfterArchiveWriteDoesNotDoubleArchiveSeconds) {
  useZone("UTC");
  const int64_t base = daysFromCivil(2026, 11, 1);
  char id[20] = {};
  for (int i = 0; i < MAX_DAYS; ++i) {
    snprintf(id, sizeof(id), "facebeef-%010d", i);
    ASSERT_TRUE(recordSession(id, (base - (MAX_DAYS - i)) * 86400 + 3600, 60, "")) << i;
  }
  ASSERT_TRUE(recordSession("facebeef-0000000064", (base + 1) * 86400 + 3600, 120, ""));
  // A replayed delivery with the same id is a no-op for the archive too.
  ASSERT_TRUE(recordSession("facebeef-0000000064", (base + 1) * 86400 + 3600, 120, ""));
  SdFakeArchiveIO archiveIo;
  uint32_t seconds = 0;
  ASSERT_TRUE(archive::daySeconds(archiveIo, static_cast<uint16_t>(base - MAX_DAYS), seconds));
  EXPECT_EQ(seconds, 60u);
}
}  // namespace
