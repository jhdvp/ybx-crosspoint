#pragma once

#include <cstdint>

struct ReaderSessionSummary {
  int64_t startEpoch = 0;  // 0 until the session has seen a trusted clock
  uint32_t durationSeconds = 0;
  uint16_t startProgressBp = 0;
  uint16_t endProgressBp = 0;
};

// Fixed-scalar active-reading summary. The owner reports completed renders and
// whether an ordinary page turn led to them; no allocation or I/O occurs here.
//
// Every interval counts: the time a page stayed on screen before a successful
// forward or backward turn, and the trailing interval closed by finish() at
// exit, sleep or restart. Time while another screen covers the page is not
// reading: pause() closes the interval when focus is lost and the next render
// starts a new one. An interval is between MIN_DWELL_SECONDS and
// MAX_DWELL_SECONDS long: shorter is a quick flip, longer is an idle book left
// open. A session shorter than MIN_SESSION_SECONDS is not reported.
class ReaderSession {
 public:
  static constexpr uint32_t MIN_DWELL_SECONDS = 2;
  static constexpr uint32_t MAX_DWELL_SECONDS = 1800;
  static constexpr uint32_t MIN_SESSION_SECONDS = 5;

  // A successful ordinary page turn happened since the previous render.
  // Skips and failed turns pass false: they close no interval.
  void noteTurn(bool succeeded);
  void onRenderComplete(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp);
  // Another screen took the page (menu, dictionary, sync): the dwell on the
  // current page is reading up to this instant, the covered span is not.
  // Repeated calls while covered add nothing.
  void pause(uint32_t monotonicMs);
  // Closes the interval for the page currently on screen and stops counting.
  // Call when reading ends (exit, sleep, restart); repeated calls add nothing
  // and a covered page contributes nothing more than pause() already counted.
  void finish(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp);
  void reset();

  bool isEmitWorthy() const { return duration >= MIN_SESSION_SECONDS; }
  ReaderSessionSummary summary() const { return {startEpoch, duration, startProgress, endProgress}; }
  // The summary as if finish() ran at this instant, without mutating the
  // session; a crash checkpoint needs the page currently being read.
  ReaderSessionSummary projected(uint32_t monotonicMs, int progressBp) const;

 private:
  static uint16_t clampProgress(int progressBp);
  void addInterval(uint32_t elapsedSeconds, int64_t epoch, uint16_t fromProgress, uint16_t toProgress);
  void anchorAt(uint32_t monotonicMs, int64_t trustedEpoch, uint16_t progress);

  uint32_t anchorMs = 0;
  int64_t anchorEpoch = 0;
  int64_t startEpoch = 0;
  uint32_t duration = 0;
  uint16_t anchorProgress = 0;
  uint16_t startProgress = 0;
  uint16_t endProgress = 0;
  bool hasAnchor = false;
  bool pendingTurn = false;
  bool covered = false;
};
