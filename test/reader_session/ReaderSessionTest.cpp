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

}  // namespace
