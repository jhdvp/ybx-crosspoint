#pragma once

#include <cstddef>
#include <cstdint>

// Bounded native model for the account's Yandex achievements as served by
// GET /api/v1/crosspoint/achievements. The endpoint returns the same cards as
// the device API: an award has no earned timestamp (earned_at is null), so
// first_observed_at is kept separately and must never be shown as an earned
// date. YBX-owned achievements (ybx_reader) are empty on the service and are
// not a second rewards system.
namespace ybxach {

// One page per section, matching the service's default limit. A bounded
// response keeps the whole snapshot in a fixed ~10 KB member instead of an
// unbounded ArduinoJson document; has_more/truncated tell the screen that the
// server holds more.
constexpr int MAX_RECEIVED = 20;
constexpr int MAX_PENDING = 20;
constexpr size_t ID_LEN = 40;  // UUID is 36 bytes
constexpr size_t TITLE_LEN = 72;
constexpr size_t DESC_LEN = 120;

struct Award {
  char id[ID_LEN] = {};
  char title[TITLE_LEN] = {};
  char description[DESC_LEN] = {};
  int16_t progress = -1;  // current value; -1 unknown
  int16_t target = -1;    // target value; -1 unknown
  int16_t percent = -1;   // 0-100; -1 unknown
  bool earnedKnown = false;
  int64_t earnedAt = 0;    // UTC epoch; null on the current service
  int64_t observedAt = 0;  // when YBX first saw the award, UTC epoch
};

struct Snapshot {
  Award received[MAX_RECEIVED];
  Award pending[MAX_PENDING];
  uint8_t receivedCount = 0;
  uint8_t pendingCount = 0;
  bool hasMoreReceived = false;
  bool hasMorePending = false;
  bool truncated = false;  // a section exceeded the in-RAM cap
  int64_t checkedAt = 0;   // service check time, not proof of a refresh
  int64_t fetchedAt = 0;   // device fetch time
};

// Parses one response body into `out` (which is reset first). Accepts the
// documented nested form ({"yandex":{...}}) and a flat one. Returns false on
// a malformed document or a non-object root; `out` must then be discarded.
bool parse(const char* json, size_t len, Snapshot& out);

// UTC epoch to local "YYYY-MM-DD" / "YYYY-MM-DD HH:MM". False for epoch <= 0
// or a too-small buffer.
bool formatDate(int64_t epoch, char* out, size_t size);
bool formatDateTime(int64_t epoch, char* out, size_t size);

// Server timestamps are Unix seconds (number or numeric string) or ISO-8601;
// returns 0 when absent or unparseable.
int64_t parseTimestamp(const char* text, size_t len);

inline bool hasAwards(const Snapshot& snapshot) { return snapshot.receivedCount > 0 || snapshot.pendingCount > 0; }

}  // namespace ybxach
