#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

// Device-local reading statistics, fed by the same reader sessions the YBX
// plugin receives. Deliberately separate from any server-side numbers: the
// totals here are what this device has recorded, and a day the device never
// saw a session for is "no data", not zero.
//
// Storage is two bounded binary files under /.crosspoint (see ReadingStats.cpp):
// cumulative totals plus a 64-day ring for the calendar, and at most 32 books.
// The model itself is header-only so the host tests can exercise day splits,
// ring eviction, dedupe and serialization without an SD card.
namespace readingstats {

inline constexpr int MAX_DAYS = 64;
inline constexpr int MAX_BOOKS = 32;
inline constexpr size_t MAX_BOOK_PATH = 96;
inline constexpr size_t MAX_ID_LENGTH = 19;
inline constexpr int MAX_SESSION_SEGMENTS = 8;

// Local calendar day as days since 1970-01-01, so DST and month lengths never
// enter the arithmetic.
struct Day {
  uint16_t ordinal = 0;
  uint32_t seconds = 0;
  uint16_t sessions = 0;
};

struct State {
  uint32_t totalSeconds = 0;  // dated and undated
  uint32_t totalSessions = 0;
  uint32_t undatedSeconds = 0;  // sessions the clock could not place on a day
  uint32_t undatedSessions = 0;
  uint16_t activeDays = 0;  // distinct dated days ever recorded
  uint16_t lastDay = 0;     // newest day with data, 0 when none
  uint32_t lastSessionEpoch = 0;
  char lastSessionId[MAX_ID_LENGTH + 1] = {};
  uint8_t dayCount = 0;
  Day days[MAX_DAYS];  // ascending by ordinal, the newest MAX_DAYS days
};

struct Book {
  char path[MAX_BOOK_PATH] = {};
  uint32_t seconds = 0;
  uint16_t sessions = 0;
  uint16_t lastDay = 0;
};

struct SessionSegment {
  uint16_t day = 0;
  uint32_t seconds = 0;
};

constexpr size_t STATE_HEADER_BYTES = 56;
constexpr size_t DAY_RECORD_BYTES = 8;
constexpr size_t MAX_STATE_BYTES = STATE_HEADER_BYTES + MAX_DAYS * DAY_RECORD_BYTES;
constexpr size_t BOOKS_HEADER_BYTES = 8;
constexpr size_t BOOK_RECORD_BYTES = MAX_BOOK_PATH + 8;
constexpr size_t MAX_BOOKS_BYTES = BOOKS_HEADER_BYTES + MAX_BOOKS * BOOK_RECORD_BYTES;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// civil-from-days inverse). Range covers every day a uint16 ordinal can hold.
constexpr int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

constexpr void civilFromDays(const int64_t days, int& year, unsigned& month, unsigned& day) {
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  month = mp < 10 ? mp + 3 : mp - 9;
  day = doy - (153 * mp + 2) / 5 + 1;
  year = y + (month <= 2);
}

inline uint16_t ordinalFromTm(const std::tm& local) {
  const int64_t days = daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                     static_cast<unsigned>(local.tm_mday));
  return days >= 0 && days <= UINT16_MAX ? static_cast<uint16_t>(days) : 0;
}

// Local day for an epoch second, 0 when the value cannot be converted (callers
// treat 0 as undated, which is also what an unset clock produces).
inline uint16_t localDayOrdinal(const int64_t epoch) {
  if (epoch <= 0) return 0;
  const std::time_t value = static_cast<std::time_t>(epoch);
  std::tm local{};
  if (localtime_r(&value, &local) == nullptr) return 0;
  return ordinalFromTm(local);
}

constexpr uint32_t saturatingAdd(const uint32_t a, const uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

constexpr uint16_t saturatingAdd16(const uint16_t a, const uint16_t b) {
  return static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, static_cast<uint32_t>(a) + b));
}

// Splits a session across local midnights. Seconds always sum to the session
// duration; when a zone's midnight cannot be resolved the remainder stays on
// the day it started. Returns false when the start time itself is unusable.
inline bool splitSession(const int64_t startEpoch, const uint32_t duration, SessionSegment* out, const int capacity,
                         int& count) {
  count = 0;
  if (startEpoch <= 0) return false;
  if (duration == 0 || capacity <= 0) return true;
  int64_t cursor = startEpoch;
  uint32_t remaining = duration;
  while (remaining > 0) {
    const std::time_t current = static_cast<std::time_t>(cursor);
    std::tm local{};
    if (localtime_r(&current, &local) == nullptr) {
      if (count > 0) {
        out[count - 1].seconds = saturatingAdd(out[count - 1].seconds, remaining);
        return true;
      }
      return false;
    }
    uint32_t segment = remaining;
    std::tm next = local;
    next.tm_hour = 0;
    next.tm_min = 0;
    next.tm_sec = 0;
    next.tm_mday += 1;
    next.tm_isdst = -1;
    const std::time_t midnight = mktime(&next);
    if (midnight > current) {
      const uint64_t untilMidnight = static_cast<uint64_t>(midnight) - static_cast<uint64_t>(current);
      if (untilMidnight < segment) segment = static_cast<uint32_t>(untilMidnight);
    }
    if (segment == 0) segment = remaining;
    const uint16_t day = ordinalFromTm(local);
    if (count > 0 && out[count - 1].day == day) {
      out[count - 1].seconds = saturatingAdd(out[count - 1].seconds, segment);
    } else if (count < capacity) {
      out[count++] = {day, segment};
    } else {
      out[count - 1].seconds = saturatingAdd(out[count - 1].seconds, remaining);
      return true;
    }
    cursor += segment;
    remaining -= segment;
  }
  return true;
}

inline const Day* findDay(const State& state, const uint16_t ordinal) {
  for (int i = 0; i < state.dayCount; i++) {
    if (state.days[i].ordinal == ordinal) return &state.days[i];
  }
  return nullptr;
}

inline bool recordedDay(const State& state, const uint16_t ordinal) { return findDay(state, ordinal) != nullptr; }

inline void addDaySeconds(State& state, const uint16_t ordinal, const uint32_t seconds, const uint16_t sessions) {
  if (ordinal == 0 || (seconds == 0 && sessions == 0)) return;
  for (int i = 0; i < state.dayCount; i++) {
    if (state.days[i].ordinal != ordinal) continue;
    state.days[i].seconds = saturatingAdd(state.days[i].seconds, seconds);
    state.days[i].sessions = saturatingAdd16(state.days[i].sessions, sessions);
    return;
  }
  int pos = 0;
  while (pos < state.dayCount && state.days[pos].ordinal < ordinal) pos++;
  if (state.dayCount == MAX_DAYS) {
    // A day older than the ring's window still counts as active, but the ring
    // keeps the newest days only.
    if (ordinal <= state.days[0].ordinal) {
      state.activeDays = saturatingAdd16(state.activeDays, 1);
      return;
    }
    std::memmove(&state.days[0], &state.days[1], (MAX_DAYS - 1) * sizeof(Day));
    state.dayCount--;
    if (pos > 0) pos--;
  }
  std::memmove(&state.days[pos + 1], &state.days[pos], (state.dayCount - pos) * sizeof(Day));
  state.days[pos] = Day{ordinal, seconds, sessions};
  state.dayCount++;
  state.activeDays = saturatingAdd16(state.activeDays, 1);
}

// Records one finished reader session. Returns the day the session started on,
// or 0 when the clock could not date it (those seconds stay in the totals and
// are reported separately).
inline uint16_t addSession(State& state, const int64_t startEpoch, const uint32_t duration) {
  if (duration == 0) return 0;
  state.totalSeconds = saturatingAdd(state.totalSeconds, duration);
  state.totalSessions = saturatingAdd(state.totalSessions, 1);
  SessionSegment segments[MAX_SESSION_SEGMENTS];
  int count = 0;
  if (!splitSession(startEpoch, duration, segments, MAX_SESSION_SEGMENTS, count) || count == 0) {
    state.undatedSeconds = saturatingAdd(state.undatedSeconds, duration);
    state.undatedSessions++;
    return 0;
  }
  for (int i = 0; i < count; i++) {
    addDaySeconds(state, segments[i].day, segments[i].seconds, i == 0 ? 1 : 0);
    if (segments[i].day > state.lastDay) state.lastDay = segments[i].day;
  }
  if (startEpoch > 0) {
    const uint32_t epoch =
        startEpoch > static_cast<int64_t>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(startEpoch);
    if (epoch > state.lastSessionEpoch) state.lastSessionEpoch = epoch;
  }
  return segments[0].day;
}

inline uint32_t sumDays(const State& state, const uint16_t first, const uint16_t last) {
  uint32_t sum = 0;
  for (int i = 0; i < state.dayCount; i++) {
    const Day& day = state.days[i];
    if (day.ordinal >= first && day.ordinal <= last) sum = saturatingAdd(sum, day.seconds);
  }
  return sum;
}

// Consecutive recorded days ending today (or yesterday, so the streak does not
// vanish before the day is over). `clipped` reports that the walk reached the
// oldest ring entry, so the real streak may be longer than MAX_DAYS.
inline int streakDays(const State& state, const uint16_t today, bool& clipped) {
  clipped = false;
  if (state.dayCount == 0 || today == 0) return 0;
  uint16_t cursor = 0;
  if (recordedDay(state, today)) {
    cursor = today;
  } else if (recordedDay(state, static_cast<uint16_t>(today - 1))) {
    cursor = static_cast<uint16_t>(today - 1);
  } else {
    return 0;
  }
  int streak = 0;
  while (recordedDay(state, cursor)) {
    streak++;
    if (cursor == 0) break;
    cursor--;
  }
  clipped = streak == MAX_DAYS && state.dayCount == MAX_DAYS;
  return streak;
}

inline bool isRecorded(const State& state, const char* id) {
  return id != nullptr && id[0] != '\0' && std::strncmp(state.lastSessionId, id, MAX_ID_LENGTH) == 0;
}

inline void noteSession(State& state, const char* id, const int64_t startEpoch) {
  if (id == nullptr || id[0] == '\0') return;
  std::strncpy(state.lastSessionId, id, MAX_ID_LENGTH);
  state.lastSessionId[MAX_ID_LENGTH] = '\0';
  if (startEpoch > 0) {
    const uint32_t epoch =
        startEpoch > static_cast<int64_t>(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(startEpoch);
    if (epoch > state.lastSessionEpoch) state.lastSessionEpoch = epoch;
  }
}

// Copies at most size-1 bytes without cutting a UTF-8 sequence in half.
inline void copyText(char* dest, const size_t size, const char* source) {
  if (size == 0) return;
  if (source == nullptr) {
    dest[0] = '\0';
    return;
  }
  size_t length = std::min(std::strlen(source), size - 1);
  while (length > 0 && (static_cast<unsigned char>(source[length]) & 0xC0) == 0x80) length--;
  std::memcpy(dest, source, length);
  dest[length] = '\0';
}

// Adds one finished session's seconds to a book entry, keeping the table
// bounded: the least recently read book makes room for a new one.
inline void addBook(Book* books, int& count, const std::string& path, const uint32_t seconds, const uint16_t sessions,
                    const uint16_t lastDay) {
  if (books == nullptr || count < 0 || path.empty() || seconds == 0) return;
  // Paths are stored truncated, so the lookup must use the same bounded key;
  // otherwise a long path would add a new row on every session.
  char key[MAX_BOOK_PATH];
  copyText(key, sizeof(key), path.c_str());
  if (key[0] == '\0') return;
  int slot = -1;
  for (int i = 0; i < count; i++) {
    if (std::strcmp(key, books[i].path) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0 && count < MAX_BOOKS) slot = count++;
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < count; i++) {
      if (books[i].lastDay < books[slot].lastDay) slot = i;
    }
  }
  if (slot < count && std::strcmp(key, books[slot].path) != 0) {
    books[slot] = Book{};
    copyText(books[slot].path, sizeof(books[slot].path), key);
  }
  books[slot].seconds = saturatingAdd(books[slot].seconds, seconds);
  books[slot].sessions = saturatingAdd16(books[slot].sessions, sessions);
  if (lastDay > books[slot].lastDay) books[slot].lastDay = lastDay;
}

namespace detail {

inline void putU16(uint8_t*& out, const uint16_t value) {
  *out++ = static_cast<uint8_t>(value & 0xFF);
  *out++ = static_cast<uint8_t>(value >> 8);
}

inline void putU32(uint8_t*& out, const uint32_t value) {
  *out++ = static_cast<uint8_t>(value & 0xFF);
  *out++ = static_cast<uint8_t>((value >> 8) & 0xFF);
  *out++ = static_cast<uint8_t>((value >> 16) & 0xFF);
  *out++ = static_cast<uint8_t>(value >> 24);
}

inline bool takeU8(const uint8_t*& in, const uint8_t* end, uint8_t& value) {
  if (in >= end) return false;
  value = *in++;
  return true;
}

inline bool takeU16(const uint8_t*& in, const uint8_t* end, uint16_t& value) {
  if (end - in < 2) return false;
  value = static_cast<uint16_t>(in[0] | (in[1] << 8));
  in += 2;
  return true;
}

inline bool takeU32(const uint8_t*& in, const uint8_t* end, uint32_t& value) {
  if (end - in < 4) return false;
  value = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) | (static_cast<uint32_t>(in[2]) << 16) |
          (static_cast<uint32_t>(in[3]) << 24);
  in += 4;
  return true;
}

inline void putBytes(uint8_t*& out, const void* data, const size_t size) {
  std::memcpy(out, data, size);
  out += size;
}

}  // namespace detail

inline size_t serializeState(const State& state, uint8_t* out, const size_t capacity) {
  if (out == nullptr || state.dayCount > MAX_DAYS) return 0;
  const size_t length = STATE_HEADER_BYTES + static_cast<size_t>(state.dayCount) * DAY_RECORD_BYTES;
  if (capacity < length) return 0;
  uint8_t* p = out;
  detail::putBytes(p, "CPS1", 4);
  *p++ = 1;  // format version
  *p++ = state.dayCount;
  detail::putU16(p, 0);
  detail::putU32(p, state.totalSeconds);
  detail::putU32(p, state.totalSessions);
  detail::putU32(p, state.undatedSeconds);
  detail::putU32(p, state.undatedSessions);
  detail::putU16(p, state.activeDays);
  detail::putU16(p, state.lastDay);
  detail::putU32(p, state.lastSessionEpoch);
  char id[MAX_ID_LENGTH + 1] = {};
  std::memcpy(id, state.lastSessionId, MAX_ID_LENGTH);
  detail::putBytes(p, id, sizeof(id));
  detail::putU32(p, 0);
  for (int i = 0; i < state.dayCount; i++) {
    detail::putU16(p, state.days[i].ordinal);
    detail::putU32(p, state.days[i].seconds);
    detail::putU16(p, state.days[i].sessions);
  }
  return static_cast<size_t>(p - out);
}

inline bool parseState(const uint8_t* in, const size_t length, State& state) {
  if (in == nullptr || length < STATE_HEADER_BYTES) return false;
  const uint8_t* p = in;
  const uint8_t* end = in + length;
  if (std::memcmp(p, "CPS1", 4) != 0) return false;
  p += 4;
  uint8_t version = 0;
  uint8_t dayCount = 0;
  uint16_t reserved16 = 0;
  uint32_t reserved32 = 0;
  if (!detail::takeU8(p, end, version) || version != 1) return false;
  if (!detail::takeU8(p, end, dayCount) || dayCount > MAX_DAYS) return false;
  if (!detail::takeU16(p, end, reserved16)) return false;
  State next;
  if (!detail::takeU32(p, end, next.totalSeconds) || !detail::takeU32(p, end, next.totalSessions) ||
      !detail::takeU32(p, end, next.undatedSeconds) || !detail::takeU32(p, end, next.undatedSessions) ||
      !detail::takeU16(p, end, next.activeDays) || !detail::takeU16(p, end, next.lastDay) ||
      !detail::takeU32(p, end, next.lastSessionEpoch)) {
    return false;
  }
  if (end - p < static_cast<ptrdiff_t>(MAX_ID_LENGTH + 1 + 4)) return false;
  std::memcpy(next.lastSessionId, p, MAX_ID_LENGTH + 1);
  next.lastSessionId[MAX_ID_LENGTH] = '\0';
  p += MAX_ID_LENGTH + 1;
  if (!detail::takeU32(p, end, reserved32)) return false;
  (void)reserved16;
  (void)reserved32;
  if (length < STATE_HEADER_BYTES + static_cast<size_t>(dayCount) * DAY_RECORD_BYTES) return false;
  uint16_t previous = 0;
  for (int i = 0; i < dayCount; i++) {
    Day& day = next.days[i];
    if (!detail::takeU16(p, end, day.ordinal) || !detail::takeU32(p, end, day.seconds) ||
        !detail::takeU16(p, end, day.sessions)) {
      return false;
    }
    if (day.ordinal == 0 || day.ordinal <= previous) return false;
    previous = day.ordinal;
    if (day.seconds == 0 && day.sessions == 0) day.seconds = 1;  // keep the day active
  }
  next.dayCount = dayCount;
  state = next;
  return true;
}

inline size_t serializeBookRecord(const Book& book, uint8_t* out, const size_t capacity) {
  if (out == nullptr || capacity < BOOK_RECORD_BYTES || book.path[0] == '\0') return 0;
  uint8_t* p = out;
  char path[MAX_BOOK_PATH] = {};
  std::memcpy(path, book.path, MAX_BOOK_PATH - 1);
  detail::putBytes(p, path, sizeof(path));
  detail::putU32(p, book.seconds);
  detail::putU16(p, book.sessions);
  detail::putU16(p, book.lastDay);
  return BOOK_RECORD_BYTES;
}

inline bool parseBookRecord(const uint8_t* in, const size_t length, Book& book) {
  if (in == nullptr || length < BOOK_RECORD_BYTES) return false;
  Book next;
  std::memcpy(next.path, in, MAX_BOOK_PATH - 1);
  next.path[MAX_BOOK_PATH - 1] = '\0';
  if (next.path[0] == '\0') return false;
  const uint8_t* p = in + MAX_BOOK_PATH;
  const uint8_t* end = in + BOOK_RECORD_BYTES;
  if (!detail::takeU32(p, end, next.seconds) || !detail::takeU16(p, end, next.sessions) ||
      !detail::takeU16(p, end, next.lastDay)) {
    return false;
  }
  if (next.seconds == 0 || next.sessions == 0) return false;
  book = next;
  return true;
}

inline size_t serializeBooks(const Book* books, const int count, uint8_t* out, const size_t capacity) {
  if (books == nullptr || out == nullptr || count < 0 || count > MAX_BOOKS) return 0;
  const size_t length = BOOKS_HEADER_BYTES + static_cast<size_t>(count) * BOOK_RECORD_BYTES;
  if (capacity < length) return 0;
  uint8_t* p = out;
  detail::putBytes(p, "CPB1", 4);
  *p++ = 1;
  *p++ = static_cast<uint8_t>(count);
  detail::putU16(p, 0);
  for (int i = 0; i < count; i++) {
    if (serializeBookRecord(books[i], p, BOOK_RECORD_BYTES) != BOOK_RECORD_BYTES) return 0;
    p += BOOK_RECORD_BYTES;
  }
  return static_cast<size_t>(p - out);
}

inline bool parseBooks(const uint8_t* in, const size_t length, Book* books, const int capacity, int& count) {
  count = 0;
  if (in == nullptr || length < BOOKS_HEADER_BYTES || books == nullptr || capacity <= 0) return false;
  const uint8_t* p = in;
  const uint8_t* end = in + length;
  if (std::memcmp(p, "CPB1", 4) != 0) return false;
  p += 4;
  uint8_t version = 0;
  uint8_t stored = 0;
  uint16_t reserved = 0;
  if (!detail::takeU8(p, end, version) || version != 1) return false;
  if (!detail::takeU8(p, end, stored) || stored > MAX_BOOKS) return false;
  if (!detail::takeU16(p, end, reserved)) return false;
  for (int i = 0; i < stored && count < capacity; i++) {
    Book book;
    if (!parseBookRecord(p, static_cast<size_t>(end - p), book)) return false;
    p += BOOK_RECORD_BYTES;
    books[count++] = book;
  }
  return true;
}

// Firmware side (ReadingStats.cpp): both files live under /.crosspoint and are
// written through a temp file + replace, so a power cut leaves the previous
// copy intact. Loading falls back to a leftover temp file after such a cut.
bool loadState(State& state);
bool loadBooks(Book* books, int capacity, int& count);
// Adds one finished session. `id` is the session event id, used to ignore a
// replay of the same session. Failures are logged; totals never grow twice.
bool recordSession(const char* id, int64_t startEpoch, uint32_t durationSeconds, const std::string& bookPath);

}  // namespace readingstats
