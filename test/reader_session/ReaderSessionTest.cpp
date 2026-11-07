#include <gtest/gtest.h>

#include "ReaderSession.h"

namespace {

TEST(ReaderSession, UsesOnlyBoundedScalarState) { EXPECT_LE(sizeof(ReaderSession), 48u); }

void anchor(ReaderSession& session, uint32_t ms, int64_t epoch, int progress) {
  session.onRenderComplete(ms, epoch, progress);
}

void turnAndRender(ReaderSession& session, bool succeeded, uint32_t ms, int64_t epoch, int progress) {
  session.noteTurn(succeeded);
  session.onRenderComplete(ms, epoch, progress);
}

TEST(ReaderSession, IgnoresDwellsUnderTwoSeconds) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 2999, 102, 1100);

  EXPECT_EQ(session.summary().durationSeconds, 0u);
  EXPECT_FALSE(session.isEmitWorthy());
}

TEST(ReaderSession, CountsForwardAndBackwardDwellsAndKeepsFirstAnchor) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 6000, 105, 1100);
  turnAndRender(session, true, 9000, 108, 1050);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.startEpoch, 100);
  EXPECT_EQ(summary.durationSeconds, 8u);
  EXPECT_EQ(summary.startProgressBp, 1000);
  EXPECT_EQ(summary.endProgressBp, 1050);
  EXPECT_TRUE(session.isEmitWorthy());
}

TEST(ReaderSession, TrailingIntervalCountsAtExitOrSleep) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 4000, 103, 1100);
  session.finish(9000, 108, 1200);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.durationSeconds, 8u);
  EXPECT_EQ(summary.startProgressBp, 1000);
  EXPECT_EQ(summary.endProgressBp, 1200);
  EXPECT_TRUE(session.isEmitWorthy());
}

TEST(ReaderSession, SessionNeedsFiveSeconds) {
  ReaderSession shortSession;
  anchor(shortSession, 1000, 100, 1000);
  turnAndRender(shortSession, true, 4000, 103, 1100);
  shortSession.finish(4500, 103, 1100);
  EXPECT_EQ(shortSession.summary().durationSeconds, 3u);
  EXPECT_FALSE(shortSession.isEmitWorthy());

  ReaderSession longSession;
  anchor(longSession, 1000, 100, 1000);
  turnAndRender(longSession, true, 4000, 103, 1100);
  longSession.finish(7000, 106, 1200);
  EXPECT_EQ(longSession.summary().durationSeconds, 6u);
  EXPECT_TRUE(longSession.isEmitWorthy());
}

TEST(ReaderSession, RedrawWithoutTurnKeepsTheReadingIntervalOpen) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  anchor(session, 5000, 104, 1100);  // menu or forced refresh: same page
  turnAndRender(session, true, 10000, 109, 1200);

  EXPECT_EQ(session.summary().durationSeconds, 9u);
}

TEST(ReaderSession, FailedTurnAndSkipDoNotCloseAnIntervalButDoNotEraseTime) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, false, 5000, 104, 1100);  // failed turn: no interval
  turnAndRender(session, true, 8000, 107, 1200);   // ordinary turn closes both

  EXPECT_EQ(session.summary().durationSeconds, 7u);
}

TEST(ReaderSession, CapsOneIntervalAtThirtyMinutes) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 3601000, 3700, 2000);

  EXPECT_EQ(session.summary().durationSeconds, 1800u);
}

TEST(ReaderSession, ClampsProgressRange) {
  ReaderSession session;
  anchor(session, 1000, 100, -50);
  turnAndRender(session, true, 7000, 106, 12000);
  session.finish(13000, 112, 5000);

  EXPECT_EQ(session.summary().startProgressBp, 0);
  EXPECT_EQ(session.summary().endProgressBp, 5000);
}

TEST(ReaderSession, UnknownClockStillCountsReading) {
  ReaderSession session;
  anchor(session, 1000, 0, 1000);
  turnAndRender(session, true, 7000, 0, 1100);
  session.finish(9000, 0, 1200);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.startEpoch, 0);
  EXPECT_EQ(summary.durationSeconds, 8u);
  EXPECT_TRUE(session.isEmitWorthy());
}

TEST(ReaderSession, ProjectionIncludesTheOpenIntervalWithoutMutating) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 4000, 103, 1100);

  const ReaderSessionSummary projected = session.projected(10000, 1200);
  EXPECT_EQ(projected.durationSeconds, 9u);
  EXPECT_EQ(projected.startProgressBp, 1000);
  EXPECT_EQ(projected.endProgressBp, 1200);
  EXPECT_EQ(session.summary().durationSeconds, 3u);

  session.finish(10000, 109, 1200);
  EXPECT_EQ(session.summary().durationSeconds, 9u);
}

TEST(ReaderSession, FinishIsIdempotent) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  session.finish(8000, 107, 1100);
  EXPECT_EQ(session.summary().durationSeconds, 7u);
  session.finish(20000, 119, 1200);
  EXPECT_EQ(session.summary().durationSeconds, 7u);
}

TEST(ReaderSession, ResetClearsSummaryAndAnchor) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  session.noteTurn(true);
  session.reset();
  session.onRenderComplete(12000, 111, 2000);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.startEpoch, 0);
  EXPECT_EQ(summary.durationSeconds, 0u);
  EXPECT_EQ(summary.startProgressBp, 0);
  EXPECT_EQ(summary.endProgressBp, 0);
  EXPECT_FALSE(session.isEmitWorthy());
}

TEST(ReaderSession, MonotonicElapsedHandlesMillisWrap) {
  ReaderSession session;
  anchor(session, 0xfffffc18u, 100, 1000);
  turnAndRender(session, true, 4000u, 105, 1100);

  EXPECT_EQ(session.summary().durationSeconds, 5u);
}

TEST(ReaderSession, CoveredSpanIsNotReadingTime) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  turnAndRender(session, true, 6000, 105, 1100);  // 5 s on page one
  session.pause(11000);                           // dictionary opens: 5 s on page two
  session.pause(16000);                           // a screen pushed over the dictionary
  session.onRenderComplete(611000, 610, 1100);    // focus returns, same page
  turnAndRender(session, true, 616000, 615, 1200);
  session.finish(621000, 620, 1300);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.durationSeconds, 20u);  // 5+5+5+5, the 10-minute lookup excluded
  EXPECT_EQ(summary.startEpoch, 100);
  EXPECT_EQ(summary.startProgressBp, 1000);
  EXPECT_EQ(summary.endProgressBp, 1300);
}

TEST(ReaderSession, CheckpointAndFinishWhileCoveredAddNothing) {
  ReaderSession session;
  anchor(session, 1000, 100, 1000);
  session.pause(3000);  // the 2 s dwell counts, the covered span does not

  EXPECT_EQ(session.projected(600000, 1400).durationSeconds, 2u);
  session.finish(600000, 600, 1400);  // sleep with the menu still open

  EXPECT_EQ(session.summary().durationSeconds, 2u);
  EXPECT_EQ(session.summary().endProgressBp, 1000);
}

TEST(ReaderSession, LateClockSyncStillPlacesTheSessionStart) {
  ReaderSession session;
  anchor(session, 1000, 0, 1000);  // cold device: no clock yet
  turnAndRender(session, true, 6000, 0, 1100);
  // SNTP landed before this render: the dwell it starts is dated 1730000060 and
  // 10 s were already counted, so the session began at 1730000050.
  turnAndRender(session, true, 11000, 1730000060, 1200);
  session.finish(16000, 1730000065, 1300);

  const ReaderSessionSummary summary = session.summary();
  EXPECT_EQ(summary.durationSeconds, 15u);
  EXPECT_EQ(summary.startEpoch, 1730000050);
  EXPECT_EQ(summary.startProgressBp, 1000);
}

TEST(ReaderSession, ProjectionAdoptsALateClock) {
  ReaderSession session;
  anchor(session, 1000, 0, 1000);
  turnAndRender(session, true, 6000, 0, 1100);
  // The clock arrived with this turn, so the dwell it starts is dated.
  turnAndRender(session, true, 11000, 1730000060, 1200);

  const ReaderSessionSummary projected = session.projected(16000, 1300);
  EXPECT_EQ(projected.durationSeconds, 15u);
  EXPECT_EQ(projected.startEpoch, 1730000050);
  EXPECT_EQ(session.summary().startEpoch, 0);  // projection does not adopt it
}

TEST(ReaderSession, UnknownClockKeepsStartAtZero) {
  ReaderSession session;
  anchor(session, 1000, 0, 1000);
  turnAndRender(session, true, 6000, 0, 1100);
  session.finish(11000, 0, 1200);

  EXPECT_EQ(session.summary().startEpoch, 0);
}

}  // namespace
