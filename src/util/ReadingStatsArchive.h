#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Logging.h"
#include "ReadingStats.h"
#include "ReadingStatsSnapshot.h"

// Monthly archive of daily reading seconds, extending the bounded MAX_DAYS ring
// toward unlimited history without growing RAM use. One month is two small
// power-safe slots on the SD card; the dashboard chart keeps reading the ring
// and is unaffected.
//
// Layout (140 bytes per slot), little-endian:
//   12-byte header: version 1, magic "YDR", first day (days since 1970-01-01
//                   for the 1st of the month), generation
//   31 x uint32 seconds, indexed by day of month minus one (zero beyond the
//                   month length)
//   4-byte CRC-32 over the preceding 136 bytes
//
// The file scheme follows CrossInk 2.0's DailyReadingStats (uxjulia/CrossInk,
// MIT license): alternating two-slot month files with a generation counter and
// a verified write, so a power cut always leaves the newest committed copy.
//
// IO contract (implemented for the SD card in ReadingStats.cpp, faked in the
// host tests; all buffers are caller-owned so nothing is held on the heap):
//   exists(const char* path)
//   read(const char* path, uint8_t* dst, size_t cap, size_t& size)
//   write(const char* path, const uint8_t* src, size_t size)  // sync + close
namespace readingstats::archive {

inline constexpr char DIRECTORY[] = "/.crosspoint/daily_reading";
inline constexpr size_t MONTH_BYTES = 12 + 31 * sizeof(uint32_t) + sizeof(uint32_t);
inline constexpr int DAYS_PER_MONTH = 31;

// One bounded month stays on the stack during archive reads and writes.
struct Month {
  int8_t slot = -1;  // newest valid copy, -1 when none
  uint32_t generation = 0;
  std::array<uint32_t, DAYS_PER_MONTH> seconds{};
};
static_assert(sizeof(Month) <= 256, "month buffers stay on the stack");

inline char slotSuffix(const int8_t slot) { return slot == 0 ? 'a' : 'b'; }

inline bool leapYear(const unsigned year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

inline unsigned daysInMonth(const unsigned year, const unsigned month) {
  static constexpr unsigned LENGTHS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && leapYear(year)) return 29;
  return LENGTHS[month - 1];
}

// Range that holds `ordinal`: the day ordinal of the first of that month and
// the month's length. False when the ordinal cannot map to a calendar month.
inline bool monthRange(const uint16_t ordinal, uint32_t& firstDay, uint8_t& days) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(ordinal, year, month, day);
  days = static_cast<uint8_t>(daysInMonth(year, month));
  if (days == 0) return false;
  firstDay = static_cast<uint32_t>(daysFromCivil(year, month, 1));
  return true;
}

inline void monthPath(const uint32_t firstDay, const int8_t slot, char (&path)[48]) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(firstDay, year, month, day);
  snprintf(path, sizeof(path), "%s/%04u-%02u.%c.bin", DIRECTORY, year, month, slotSuffix(slot));
}

namespace detail {

inline void put32(uint8_t* out, const uint32_t value) {
  for (size_t i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (i * 8));
}

inline uint32_t take32(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | static_cast<uint32_t>(in[1]) << 8 | static_cast<uint32_t>(in[2]) << 16 |
         static_cast<uint32_t>(in[3]) << 24;
}

}  // namespace detail

inline void serializeMonth(const Month& month, const uint32_t firstDay, uint8_t (&out)[MONTH_BYTES]) {
  out[0] = 1;
  out[1] = 'Y';
  out[2] = 'D';
  out[3] = 'R';
  detail::put32(out + 4, firstDay);
  detail::put32(out + 8, month.generation);
  for (int i = 0; i < DAYS_PER_MONTH; ++i) detail::put32(out + 12 + 4 * i, month.seconds[i]);
  detail::put32(out + MONTH_BYTES - 4, snapshot::crc32(out, MONTH_BYTES - 4));
}

// Checks structure, magic, the expected first day, month-length bounds, and the
// checksum. A newer format version is never parsed (and never overwritten),
// whatever its size; only an exact-size, well-formed version 1 record loads.
template <class IO>
bool readSlot(IO& io, const uint32_t firstDay, const uint8_t days, const int8_t slot, bool& exists, bool& valid,
              uint32_t& generation, Month& out) {
  exists = false;
  valid = false;
  generation = 0;
  if (slot != 0 && slot != 1) return true;
  char path[48];
  monthPath(firstDay, slot, path);
  if (!io.exists(path)) return true;
  exists = true;
  uint8_t raw[MONTH_BYTES];
  size_t size = 0;
  if (!io.read(path, raw, sizeof(raw), size)) {
    LOG_ERR("RSTA", "archive read failed: %s", path);
    return false;
  }
  out = Month{};
  // Version precedes the size check so a torn or short newer-format copy is
  // still identified and refused, never bypassed in favor of its v1 sibling.
  if (size != 0 && raw[0] > 1) {
    LOG_ERR("RSTA", "refusing newer archive copy: %s", path);
    return false;
  }
  if (size != MONTH_BYTES) return true;  // short or oversize copy: present, but not valid
  if (raw[0] != 1 || std::memcmp(raw + 1, "YDR", 3) != 0 || detail::take32(raw + 4) != firstDay) return true;
  generation = detail::take32(raw + 8);
  if (detail::take32(raw + MONTH_BYTES - 4) != snapshot::crc32(raw, MONTH_BYTES - 4)) return true;
  // Days beyond the month length must be empty, or the copy is a torn write.
  for (int i = 0; i < DAYS_PER_MONTH; ++i) {
    const uint32_t seconds = detail::take32(raw + 12 + 4 * i);
    if (static_cast<unsigned>(i) >= days && seconds != 0) return true;
    if (seconds == 0) continue;
    out.seconds[i] = seconds;
  }
  valid = true;
  return true;
}

// Older generations lose to a valid newest copy; wrap-safe generation
// comparison. I/O errors and unknown versions must never be overwritten.
template <class IO>
bool loadMonth(IO& io, const uint32_t firstDay, const uint8_t days, Month& month) {
  month = Month{};
  unsigned existingSlots = 0;
  for (int8_t slot = 0; slot < 2; ++slot) {
    bool exists = false;
    bool valid = false;
    uint32_t generation = 0;
    Month found;
    if (!readSlot(io, firstDay, days, slot, exists, valid, generation, found)) return false;
    if (exists) existingSlots |= 1u << slot;
    const uint32_t delta = generation - month.generation;
    if (valid && (month.slot < 0 || (delta != 0 && delta < 0x80000000u))) {
      month.generation = generation;
      month.slot = slot;
      month.seconds = found.seconds;
    }
  }
  if (month.slot < 0) {
    // New months start with the empty recovery copy in slot b before slot a
    // receives data, so a lone torn slot b has never held committed history
    // and is safe to reinitialize. Any other copy with no valid sibling may
    // still be the only record of committed history: retained, never rewritten.
    if (existingSlots == 2) return true;
    if (existingSlots != 0) {
      LOG_ERR("RSTA", "no valid monthly copy");
      return false;
    }
    return true;
  }
  bool exists = false;
  bool valid = false;
  uint32_t generation = 0;
  Month verified;
  if (!readSlot(io, firstDay, days, month.slot, exists, valid, generation, verified) || !valid) {
    LOG_ERR("RSTA", "monthly copy changed while reading");
    return false;
  }
  month.seconds = verified.seconds;
  return true;
}

// Writes the other slot and verifies it readback; the previous copy stays
// intact on failure. The first checkpoint of a new month carries empty data
// (generation 0), the second one commits the updated copy.
template <class IO>
bool saveMonth(IO& io, const uint32_t firstDay, const uint8_t days, Month& month) {
  const int8_t target = month.slot == 0 ? 1 : 0;
  const uint32_t generation = month.generation + 1;
  Month next = month;
  next.slot = target;
  next.generation = generation;
  uint8_t raw[MONTH_BYTES];
  serializeMonth(next, firstDay, raw);
  char path[48];
  monthPath(firstDay, target, path);
  if (!io.write(path, raw, sizeof(raw))) {
    LOG_ERR("RSTA", "archive write failed: %s", path);
    return false;
  }
  bool exists = false;
  bool valid = false;
  uint32_t verifiedGeneration = 0;
  Month verified;
  if (!readSlot(io, firstDay, days, target, exists, valid, verifiedGeneration, verified) || !valid ||
      verifiedGeneration != generation) {
    LOG_ERR("RSTA", "archive checkpoint failed; previous copy retained: %s", path);
    return false;
  }
  month = next;
  return true;
}

// Adds seconds to one calendar day's archived total. Additive and saturating:
// late-arriving seconds for an already archived day are merged again.
template <class IO>
bool addSeconds(IO& io, const uint16_t ordinal, const uint32_t seconds) {
  if (ordinal == 0 || seconds == 0) return true;
  uint32_t firstDay = 0;
  uint8_t days = 0;
  if (!monthRange(ordinal, firstDay, days)) {
    LOG_ERR("RSTA", "cannot map day %u to a month", ordinal);
    return false;
  }
  Month month;
  if (!loadMonth(io, firstDay, days, month)) return false;
  const size_t index = static_cast<size_t>(ordinal - firstDay);
  const uint32_t merged = saturatingAdd(month.seconds[index], seconds);
  if (merged == month.seconds[index]) return true;
  if (month.slot < 0) {
    // Establish the empty recovery copy first (slot b, generation 0), so an
    // interrupted data checkpoint leaves a valid empty month, not a torn or
    // half-committed one.
    Month recovery;
    recovery.slot = 0;  // saveMonth alternates to slot b when the source is slot a
    recovery.generation = UINT32_MAX;
    if (!saveMonth(io, firstDay, days, recovery)) return false;
    month.slot = recovery.slot;
    month.generation = recovery.generation;
  }
  month.seconds[index] = merged;
  return saveMonth(io, firstDay, days, month);
}

// Archived seconds for one day; 0 when nothing was archived yet (not an error).
template <class IO>
bool daySeconds(IO& io, const uint16_t ordinal, uint32_t& seconds) {
  seconds = 0;
  if (ordinal == 0) return true;
  uint32_t firstDay = 0;
  uint8_t days = 0;
  if (!monthRange(ordinal, firstDay, days)) return false;
  Month month;
  if (!loadMonth(io, firstDay, days, month)) return false;
  seconds = month.seconds[static_cast<size_t>(ordinal - firstDay)];
  return true;
}

}  // namespace readingstats::archive
