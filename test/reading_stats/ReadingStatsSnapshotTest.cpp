#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "util/ReadingStatsSnapshot.h"

namespace {
using namespace readingstats;
using namespace readingstats::snapshot;

struct FakeIO {
  std::map<std::string, std::string> disk;
  int mutation = 0;
  int failAt = -1;
  bool shortWrite = false;

  bool exists(const char* name) const { return disk.find(name) != disk.end(); }
  bool read(const char* name, std::string& bytes) const {
    const auto it = disk.find(name);
    if (it == disk.end()) return false;
    bytes = it->second;
    return true;
  }
  bool writeSync(const char* name, const std::string& bytes) {
    if (++mutation == failAt) {
      if (shortWrite) disk[name] = bytes.substr(0, bytes.size() / 2);
      return false;
    }
    disk[name] = bytes;
    return true;
  }
  bool rename(const char* from, const char* to) {
    if (++mutation == failAt) return false;
    const auto it = disk.find(from);
    if (it == disk.end() || exists(to)) return false;
    disk[to] = it->second;
    disk.erase(it);
    return true;
  }
  bool remove(const char* name) {
    if (++mutation == failAt) return false;
    return disk.erase(name) == 1;
  }
};

std::unique_ptr<Data> build(int sessions = 1) {
  auto data = std::make_unique<Data>();
  for (int i = 0; i < sessions; ++i) {
    char id[20];
    std::snprintf(id, sizeof(id), "facebeef-%010d", i);
    addSession(data->state, 1730000000 + i * 200, 180);
    noteSession(data->state, id, 1730000000 + i * 200);
    addBook(data->books, data->bookCount, "/books/example.epub", 180, 1, 20000);
  }
  return data;
}

TEST(ReadingSnapshot, RoundTripKeepsAggregateAndBookCountersTogether) {
  auto data = build(3);
  std::string raw;
  ASSERT_TRUE(serialize(*data, raw));
  EXPECT_LE(raw.size(), MAX_BYTES);
  auto restored = std::make_unique<Data>();
  ASSERT_TRUE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), *restored));
  EXPECT_EQ(restored->state.totalSeconds, 540u);
  EXPECT_EQ(restored->state.totalSessions, 3u);
  EXPECT_EQ(restored->bookCount, 1);
  EXPECT_EQ(restored->books[0].seconds, 540u);
  EXPECT_EQ(restored->books[0].sessions, 3);
  EXPECT_TRUE(isRecorded(restored->state, "facebeef-0000000000"));
}

TEST(ReadingSnapshot, ValidatesChecksumHeaderAndTruncation) {
  auto data = build();
  std::string raw;
  ASSERT_TRUE(serialize(*data, raw));
  auto restored = std::make_unique<Data>();
  EXPECT_FALSE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size() - 1, *restored));
  raw[HEADER_BYTES + 10] ^= 0x01;
  EXPECT_FALSE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), *restored));
  ASSERT_TRUE(serialize(*data, raw));
  raw[5] = MAX_BOOKS + 1;
  EXPECT_FALSE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), *restored));
  ASSERT_TRUE(serialize(*data, raw));
  raw[8] ^= 0x04;
  EXPECT_FALSE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), *restored));
  ASSERT_TRUE(serialize(*data, raw));
  raw[0] = 'X';
  EXPECT_FALSE(parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), *restored));
}

TEST(ReadingSnapshot, FirstSaveThenReplayIsIdempotent) {
  FakeIO io;
  auto data = build(1);
  ASSERT_TRUE(save(io, *data));
  auto restored = std::make_unique<Data>();
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_TRUE(isRecorded(restored->state, "facebeef-0000000000"));
  EXPECT_EQ(restored->state.totalSeconds, restored->books[0].seconds);
  auto next = build(2);
  ASSERT_TRUE(save(io, *next));
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 360u);
  EXPECT_EQ(restored->books[0].seconds, 360u);
  EXPECT_TRUE(isRecorded(restored->state, "facebeef-0000000000"));
}

TEST(ReadingSnapshot, RecoverFromCorruptPrimaryUsingBackup) {
  FakeIO io;
  auto first = build(1);
  auto second = build(2);
  ASSERT_TRUE(save(io, *first));
  ASSERT_TRUE(save(io, *second));
  io.disk[PRIMARY].back() ^= 1;
  auto restored = std::make_unique<Data>();
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 180u);
  EXPECT_EQ(restored->books[0].seconds, 180u);
  auto third = build(3);
  ASSERT_TRUE(save(io, *third));
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 540u);
  EXPECT_EQ(restored->books[0].seconds, 540u);
}

TEST(ReadingSnapshot, CorruptOnlyCopyIsNeverSilentlyReset) {
  FakeIO io;
  auto data = build();
  ASSERT_TRUE(save(io, *data));
  io.disk[PRIMARY][HEADER_BYTES] ^= 1;
  auto next = build(2);
  auto restored = std::make_unique<Data>();
  EXPECT_EQ(load(io, *restored), LoadStatus::Corrupt);
  EXPECT_FALSE(save(io, *next));
  EXPECT_EQ(load(io, *restored), LoadStatus::Corrupt);
}

TEST(ReadingSnapshot, CompletedTempWithoutPrimaryCanRecover) {
  FakeIO io;
  auto data = build();
  std::string raw;
  ASSERT_TRUE(serialize(*data, raw));
  io.disk[TEMP] = raw;
  auto restored = std::make_unique<Data>();
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 180u);
  ASSERT_TRUE(save(io, *data));
  EXPECT_TRUE(io.exists(PRIMARY));
}

TEST(ReadingSnapshot, AllFaultPointsKeepEitherOldOrNewCompleteGeneration) {
  auto first = build(1);
  auto second = build(2);
  for (int fault = 1; fault < 9; ++fault) {
    FakeIO io;
    ASSERT_TRUE(save(io, *first));
    io.mutation = 0;
    io.failAt = fault;
    io.shortWrite = true;
    const bool succeeded = save(io, *second);
    io.failAt = -1;
    auto restored = std::make_unique<Data>();
    ASSERT_EQ(load(io, *restored), LoadStatus::Loaded) << "fault " << fault;
    EXPECT_EQ(restored->state.totalSeconds, restored->books[0].seconds) << "fault " << fault;
    EXPECT_EQ(restored->state.totalSessions, restored->books[0].sessions) << "fault " << fault;
    EXPECT_TRUE(restored->state.totalSeconds == 180 || restored->state.totalSeconds == 360) << fault;
    if (succeeded) EXPECT_EQ(restored->state.totalSeconds, 360u) << fault;
  }
}

TEST(ReadingSnapshot, DirtyTempDoesNotOverrideCommittedPrimary) {
  FakeIO io;
  auto oldData = build(1);
  auto newData = build(2);
  ASSERT_TRUE(save(io, *oldData));
  ASSERT_TRUE(serialize(*newData, io.disk[TEMP]));
  auto restored = std::make_unique<Data>();
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 180u);
  EXPECT_EQ(restored->books[0].seconds, 180u);
}

TEST(ReadingSnapshot, BackupWinsOverUncommittedTempWhenPrimaryIsMissing) {
  FakeIO io;
  auto oldData = build(1);
  auto newData = build(2);
  ASSERT_TRUE(serialize(*oldData, io.disk[BACKUP]));
  ASSERT_TRUE(serialize(*newData, io.disk[TEMP]));
  auto restored = std::make_unique<Data>();
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 180u);
  EXPECT_EQ(restored->books[0].seconds, 180u);
  ASSERT_TRUE(save(io, *newData));
  ASSERT_EQ(load(io, *restored), LoadStatus::Loaded);
  EXPECT_EQ(restored->state.totalSeconds, 360u);
}

TEST(ReadingSnapshot, FirstWriteShortFailureDoesNotAcceptTruncatedData) {
  FakeIO io;
  auto first = build(1);
  io.failAt = 1;
  io.shortWrite = true;
  EXPECT_FALSE(save(io, *first));
  auto restored = std::make_unique<Data>();
  EXPECT_EQ(load(io, *restored), LoadStatus::Corrupt);
  // Repairing a corrupted first-write temp requires deliberate recovery; do
  // not silently rebuild a different history on top of garbage.
  auto second = build(2);
  EXPECT_FALSE(save(io, *second));
}

}  // namespace
