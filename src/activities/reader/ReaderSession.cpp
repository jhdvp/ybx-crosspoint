#include "ReaderSession.h"

#include <algorithm>

uint16_t ReaderSession::clampProgress(const int progressBp) {
  return static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
}

void ReaderSession::noteTurn(const bool succeeded) {
  if (succeeded) pendingTurn = true;
}

void ReaderSession::addInterval(const uint32_t elapsedSeconds, const int64_t epoch, const uint16_t fromProgress,
                                const uint16_t toProgress) {
  if (elapsedSeconds < MIN_DWELL_SECONDS) return;
  if (duration == 0) {
    startEpoch = epoch;
    startProgress = fromProgress;
  }
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
  if (pendingTurn && hasAnchor) {
    addInterval((monotonicMs - anchorMs) / 1000, anchorEpoch, anchorProgress, progress);
  }
  // A redraw without a page turn (menu, full refresh, forced refresh) keeps
  // the anchor: the page never changed, so the time since the last page change
  // is still reading time; moving the anchor would discard it.
  if (pendingTurn || !hasAnchor) anchorAt(monotonicMs, trustedEpoch, progress);
  pendingTurn = false;
}

void ReaderSession::finish(const uint32_t monotonicMs, const int64_t trustedEpoch, const int progressBp) {
  const uint16_t progress = clampProgress(progressBp);
  if (hasAnchor) {
    addInterval((monotonicMs - anchorMs) / 1000, anchorEpoch, anchorProgress, progress);
  }
  anchorAt(monotonicMs, trustedEpoch, progress);
  hasAnchor = false;
  pendingTurn = false;
}

ReaderSessionSummary ReaderSession::projected(const uint32_t monotonicMs, const int progressBp) const {
  ReaderSessionSummary out = summary();
  if (!hasAnchor) return out;
  const uint32_t elapsedSeconds = (monotonicMs - anchorMs) / 1000;
  if (elapsedSeconds < MIN_DWELL_SECONDS) return out;
  if (out.durationSeconds == 0) {
    out.startEpoch = anchorEpoch;
    out.startProgressBp = anchorProgress;
  }
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
}
