#pragma once

#include <cstdint>

namespace readerprogress {

// Bounded, allocation-free SD write policy; never suppress an explicit flush.
// Used only for the ordinary EPUB page-turn path. ReaderSessionJournal and
// KOReaderSync have separate durability/idempotency contracts.
class ProgressWriteGate {
 public:
  static constexpr uint32_t kMaxPendingMs = 60 * 1000;
  static constexpr uint32_t kFailureRetryMs = 2000;
  static constexpr uint8_t kMaxTurns = 8;

  bool observe(const uint64_t key, const uint32_t nowMs) {
    if (!observed_ || key != lastObserved_) {
      lastObserved_ = key;
      observed_ = true;
      dirty_ = true;
      if (changes_ < kMaxTurns) ++changes_;
    }
    return due(nowMs);
  }

  bool due(const uint32_t nowMs) const {
    return dirty_ && retryAllowed(nowMs) &&
           (!persisted_ || changes_ >= kMaxTurns ||
            static_cast<uint32_t>(nowMs - lastPersistedMs_) >= kMaxPendingMs);
  }

  bool needsFlush() const { return dirty_; }
  void complete(const uint32_t nowMs, const bool success) {
    failedRecently_ = !success;
    lastAttemptMs_ = nowMs;
    if (success) {
      dirty_ = false;
      changes_ = 0;
      persisted_ = true;
      lastPersistedMs_ = nowMs;
    }
  }

 private:
  bool retryAllowed(const uint32_t nowMs) const {
    return !failedRecently_ || static_cast<uint32_t>(nowMs - lastAttemptMs_) >= kFailureRetryMs;
  }

  uint64_t lastObserved_ = 0;
  uint32_t lastPersistedMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint8_t changes_ = 0;
  bool observed_ = false;
  bool dirty_ = false;
  bool persisted_ = false;
  bool failedRecently_ = false;
};
}  // namespace readerprogress
