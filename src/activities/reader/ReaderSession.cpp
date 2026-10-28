#include "ReaderSession.h"

#include <algorithm>

namespace {
// The clock can arrive mid-session (SNTP after boot). The counted dwells began
// at most `countedSeconds` before a known epoch, so that is the best start_time
// estimate; 0 keeps the "clock never seen" meaning.
int64_t estimatedStartEpoch(const int64_t knownEpoch, const uint32_t countedSeconds) {
  return knownEpoch > static_cast<int64_t>(countedSeconds) ? knownEpoch - static_cast<int64_t>(countedSeconds) : 0;
}
}  // namespace

uint16_t ReaderSession::clampProgress(const int progressBp) {
  return static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
}

void ReaderSession::noteTurn(const bool succeeded) {
  if (succeeded) pendingTurn = true;
}

void ReaderSession::addInterval(const uint32_t elapsedSeconds, const int64_t epoch, const uint16_t fromProgress,
                                const uint16_t toProgress) {
  if (elapsedSeconds < MIN_DWELL_SECONDS) return;
  if (startEpoch == 0) startEpoch = estimatedStartEpoch(epoch, duration);
  if (duration == 0) startProgress = fromProgress;
  duration += std::min(elapsedSeconds, MAX_DWELL_SECONDS);
  endProgress = toProgress;
}

void ReaderSession::anchorAt(const uint32_t monotonicMs, const int64_t trustedEpoch, const uint16_t progress) {
  anchorMs = monotonicMs;
  anchorEpoch = trustedEpoch;
  anchorProgress = progress;
  hasAnchor = true;
}

void ReaderSession::onRenderComplete(const uint32_t monotonicMs, const int64_t trustedEpoch, const int progressBp) {
  const uint16_t progress = clampProgress(progressBp);
  if (covered) {
    // The screen came back: the covered span is not reading time, so this
    // render starts the dwell instead of closing one.
    anchorAt(monotonicMs, trustedEpoch, progress);
    covered = false;
    pendingTurn = false;
    return;
  }
  if (pendingTurn && hasAnchor) {
    addInterval((monotonicMs - anchorMs) / 1000, anchorEpoch, anchorProgress, progress);
  }
  // A redraw without a page turn (full refresh, forced refresh) keeps the
  // anchor: the page never changed, so the time since the last page change is
  // still reading time; moving the anchor would discard it.
  if (pendingTurn || !hasAnchor) anchorAt(monotonicMs, trustedEpoch, progress);
  pendingTurn = false;
}

void ReaderSession::pause(const uint32_t monotonicMs) {
  if (covered) return;
  covered = true;
  pendingTurn = false;
  if (!hasAnchor) return;
  addInterval((monotonicMs - anchorMs) / 1000, anchorEpoch, anchorProgress, anchorProgress);
  anchorAt(monotonicMs, anchorEpoch, anchorProgress);
}

void ReaderSession::finish(const uint32_t monotonicMs, const int64_t trustedEpoch, const int progressBp) {
  const uint16_t progress = clampProgress(progressBp);
  if (hasAnchor && !covered) {
    addInterval((monotonicMs - anchorMs) / 1000, anchorEpoch, anchorProgress, progress);
  }
  anchorAt(monotonicMs, trustedEpoch, progress);
  hasAnchor = false;
  pendingTurn = false;
  covered = false;
}

ReaderSessionSummary ReaderSession::projected(const uint32_t monotonicMs, const int progressBp) const {
  ReaderSessionSummary out = summary();
  if (!hasAnchor || covered) return out;
  const uint32_t elapsedSeconds = (monotonicMs - anchorMs) / 1000;
  if (elapsedSeconds < MIN_DWELL_SECONDS) return out;
  if (out.durationSeconds == 0) out.startProgressBp = anchorProgress;
  if (out.startEpoch == 0) out.startEpoch = estimatedStartEpoch(anchorEpoch, out.durationSeconds);
  out.durationSeconds += std::min(elapsedSeconds, MAX_DWELL_SECONDS);
  out.endProgressBp = clampProgress(progressBp);
  return out;
}

void ReaderSession::reset() {
  anchorMs = 0;
  anchorEpoch = 0;
  startEpoch = 0;
  duration = 0;
  anchorProgress = 0;
  startProgress = 0;
  endProgress = 0;
  hasAnchor = false;
  pendingTurn = false;
  covered = false;
}
