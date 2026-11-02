#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "util/ReadingStats.h"

namespace {

using namespace readingstats;

// Regression: recordSession() keeps only one session id (lastSessionId) and
// uses it as the sole dedup key. A replay of any earlier session therefore
// passes isRecorded(), adds its seconds again, and inflates totals. The
// contract in ReadingStats.h promises "totals never grow twice"; this test
// captures the violation so a future fix can target it directly.
TEST(ReadingStatsDedupeRegression, ReplayOfNonLatestSessionDoubleCounts) {
  State state;
  const int64_t firstEpoch = 1730000000;
  const int64_t secondEpoch = 1730001000;
  const uint32_t duration = 600;

  // First session records cleanly.
  EXPECT_FALSE(isRecorded(state, "aaaaaaaa-1730000000"));
  addSession(state, firstEpoch, duration);
  noteSession(state, "aaaaaaaa-1730000000", firstEpoch);
  EXPECT_EQ(state.totalSeconds, duration);
  EXPECT_EQ(state.totalSessions, 1u);

  // A second session becomes the new lastSessionId; the first is no longer
  // the most recent entry but must still be recognised on replay.
  addSession(state, secondEpoch, duration);
  noteSession(state, "bbbbbbbb-1730001000", secondEpoch);
  EXPECT_EQ(state.totalSeconds, duration * 2);
  EXPECT_EQ(state.totalSessions, 2u);
  EXPECT_STREQ(state.lastSessionId, "bbbbbbbb-1730001000");

  // The regression: isRecorded only compares against lastSessionId, so the
  // original id is treated as new and would be added again by recordSession().
  // This assertion documents the current broken behaviour; flip it to
  // EXPECT_TRUE once a proper dedup store lands.
  EXPECT_FALSE(isRecorded(state, "aaaaaaaa-1730000000"))
      << "isRecorded() only tracks the most recent session id; an older id "
         "replayed from the journal bypasses dedup and double-counts totals.";

  // Simulate what recordSession() does when it sees the stale id: it adds
  // the session again because isRecorded returned false. Totals must not
  // grow, but today they do.
  const uint32_t beforeSeconds = state.totalSeconds;
  const uint32_t beforeSessions = state.totalSessions;
  if (!isRecorded(state, "aaaaaaaa-1730000000")) {
    addSession(state, firstEpoch, duration);
    noteSession(state, "aaaaaaaa-1730000000", firstEpoch);
  }

  // Document the inflation. When the dedup store is fixed, replace this with
  // EXPECT_EQ(beforeSeconds, state.totalSeconds) and remove the guard above.
  EXPECT_GT(state.totalSeconds, beforeSeconds)
      << "Expected double-count until a multi-session dedup store is added.";
  EXPECT_GT(state.totalSessions, beforeSessions);
}

// The same gap applies to book totals: updateBooks() is gated only by
// recordSession()'s single-id check, so a replayed old session also adds
// its seconds to the per-book accumulator. Captured here as a reminder that
// any dedup fix must cover both the state file and the book table.
TEST(ReadingStatsDedupeRegression, BookTotalsInflateOnReplay) {
  std::array<Book, MAX_BOOKS> books{};
  int count = 0;
  const char* path = "/books/regression.epub";
  const uint32_t duration = 300;
  const uint16_t day = 20754;

  addBook(books.data(), count, path, duration, 1, day);
  ASSERT_EQ(count, 1);
  EXPECT_EQ(books[0].seconds, duration);
  EXPECT_EQ(books[0].sessions, 1);

  // Without a dedup gate at the book layer, the same call on replay adds
  // the duration again. The contract says totals never grow twice; today
  // they do. Flip to EXPECT_EQ once the gate exists.
  addBook(books.data(), count, path, duration, 1, day);
  EXPECT_GT(books[0].seconds, duration)
      << "Book seconds grew on replay; dedup must cover the book table too.";
  EXPECT_EQ(books[0].sessions, 2);
}

}  // namespace