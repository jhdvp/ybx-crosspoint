#pragma once

#include <cstdint>

namespace freeink::network {

/**
 * Bounds one logical catalog operation (feed, publication detail, license
 * exchange) by an absolute wall-clock deadline and carries a user cancel into
 * the HTTP layer. The deadline is absolute on purpose: the HTTP client's
 * per-read timeout restarts on every byte, so a server dripping bytes would
 * otherwise never trip it. All arithmetic is wraparound-safe for millis().
 */
class CatalogFetchLimiter {
 public:
  enum class Stop { None, Cancelled, TimedOut };

  // Whole-operation budget. A redirect chain, TLS retries and slow bodies all
  // draw from the same window; one operation returns control within it.
  static constexpr uint32_t DEFAULT_BUDGET_MS = 20000;
  // Cap for one socket operation (connect, header read, body read). Below the
  // budget so a single stalled socket leaves time to report the failure.
  static constexpr uint32_t MAX_SOCKET_TIMEOUT_MS = 10000;

  void begin(uint32_t now, uint32_t budgetMs = DEFAULT_BUDGET_MS) {
    active_ = true;
    stop_ = Stop::None;
    deadline_ = now + budgetMs;
  }

  void end() {
    active_ = false;
    stop_ = Stop::None;
  }

  // First reason wins: a timeout after a cancel keeps the user's reason, and a
  // late cancel does not rewrite a timeout.
  void cancel() {
    if (active_ && stop_ == Stop::None) stop_ = Stop::Cancelled;
  }

  bool active() const { return active_; }
  Stop stop() const { return stop_; }

  // Polled from the HTTP abort callback while a request is in flight. Latches
  // TimedOut at the deadline and stays latched afterwards.
  bool abortRequested(uint32_t now) {
    if (!active_) return false;
    if (stop_ == Stop::None && reached(now, deadline_)) stop_ = Stop::TimedOut;
    return stop_ != Stop::None;
  }

  uint32_t remainingMs(uint32_t now) const {
    if (!active_) return 0;
    return deadline_ - now;
  }

  // Per-request socket timeout. Zero (which the HTTP layer reads as "default")
  // only when inactive or stopped; otherwise at least 1 ms and at most
  // MAX_SOCKET_TIMEOUT_MS.
  uint32_t socketTimeoutMs(uint32_t now) const {
    if (!active_ || stop_ != Stop::None) return 0;
    const uint32_t remaining = remainingMs(now);
    if (remaining == 0) return 1;
    return remaining < MAX_SOCKET_TIMEOUT_MS ? remaining : MAX_SOCKET_TIMEOUT_MS;
  }

 private:
  static bool reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
  }

  bool active_ = false;
  Stop stop_ = Stop::None;
  uint32_t deadline_ = 0;
};

}  // namespace freeink::network
