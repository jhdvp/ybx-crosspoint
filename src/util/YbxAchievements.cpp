#include "YbxAchievements.h"

#include <StreamingJsonParser.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ybxach {
namespace {

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool readDigits(const char* s, size_t n, size_t pos, size_t count, int& out) {
  if (pos + count > n) return false;
  int value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char c = s[pos + i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

// ISO-8601 calendar date with an optional time and UTC offset. The service
// sends Unix seconds, but the shared ybx-apps fixtures carry ISO strings.
int64_t isoToEpoch(const char* s, size_t n) {
  if (n < 10 || s[4] != '-' || s[7] != '-') return 0;
  int year = 0;
  int month = 0;
  int day = 0;
  if (!readDigits(s, n, 0, 4, year) || !readDigits(s, n, 5, 2, month) || !readDigits(s, n, 8, 2, day)) return 0;
  if (month < 1 || month > 12 || day < 1 || day > 31) return 0;

  int hour = 0;
  int minute = 0;
  int second = 0;
  int offsetSeconds = 0;
  size_t pos = 10;
  if (pos < n && (s[pos] == 'T' || s[pos] == ' ')) {
    if (!readDigits(s, n, pos + 1, 2, hour) || pos + 3 >= n || s[pos + 3] != ':' ||
        !readDigits(s, n, pos + 4, 2, minute))
      return 0;
    pos += 6;
    if (pos < n && s[pos] == ':') {
      if (!readDigits(s, n, pos + 1, 2, second)) return 0;
      pos += 3;
    }
    if (pos < n && s[pos] == '.') {
      ++pos;
      while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
    }
    if (pos < n && (s[pos] == 'Z' || s[pos] == 'z')) {
      ++pos;
    } else if (pos < n && (s[pos] == '+' || s[pos] == '-')) {
      int offsetHour = 0;
      int offsetMinute = 0;
      if (!readDigits(s, n, pos + 1, 2, offsetHour) || pos + 3 >= n || s[pos + 3] != ':' ||
          !readDigits(s, n, pos + 4, 2, offsetMinute))
        return 0;
      offsetSeconds = offsetHour * 3600 + offsetMinute * 60;
      if (s[pos] == '-') offsetSeconds = -offsetSeconds;
      pos += 6;
    }
  }
  if (hour > 23 || minute > 59 || second > 59) return 0;
  return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
         hour * 3600 + minute * 60 + second - offsetSeconds;
}

// Copies at most cap-1 bytes and never splits a UTF-8 sequence. Control
// characters become spaces so one row cannot inject line breaks.
void copyUtf8(char* dst, size_t cap, const char* src, size_t len) {
  if (cap == 0) return;
  size_t n = len < cap - 1 ? len : cap - 1;
  if (n < len && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) {
    size_t k = n;
    while (k > 0 && (static_cast<unsigned char>(src[k - 1]) & 0xC0) == 0x80) --k;
    n = k > 0 ? k - 1 : 0;
  }
  for (size_t i = 0; i < n; ++i) {
    const char c = src[i];
    dst[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  dst[n] = '\0';
}

double toDouble(const char* text, size_t len) {
  char buf[32];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, text, len);
  buf[len] = '\0';
  return strtod(buf, nullptr);
}

int16_t clampCount(double value) {
  if (value < 0) return -1;
  if (value > 32767) return 32767;
  return static_cast<int16_t>(value);
}

int16_t clampPercent(double value) {
  if (value < 0) return -1;
  if (value > 100) return 100;
  return static_cast<int16_t>(value + 0.5);
}

enum class Key : uint8_t {
  None,
  Yandex,
  Received,
  Pending,
  HasMore,
  CheckedAt,
  Id,
  Title,
  Description,
  EarnedAt,
  FirstObservedAt,
  Percent,
  Progress,
  Value,
  Current,
  Target,
};

Key keyFrom(const char* key, size_t len) {
  struct Entry {
    const char* name;
    Key key;
  };
  static constexpr Entry TABLE[] = {
      {"yandex", Key::Yandex},         {"received", Key::Received},
      {"pending", Key::Pending},       {"has_more", Key::HasMore},
      {"checked_at", Key::CheckedAt},  {"id", Key::Id},
      {"title", Key::Title},           {"description", Key::Description},
      {"earned_at", Key::EarnedAt},    {"first_observed_at", Key::FirstObservedAt},
      {"percent", Key::Percent},       {"progress", Key::Progress},
      {"value", Key::Value},           {"current", Key::Current},
      {"target", Key::Target},
  };
  for (const auto& entry : TABLE) {
    if (strlen(entry.name) == len && memcmp(entry.name, key, len) == 0) return entry.key;
  }
  return Key::None;
}

enum class Ctx : uint8_t {
  Ignore,
  Root,
  Yandex,
  Received,
  Pending,
  Award,
  Percent,
  Progress,
  Current,
  Target,
  HasMore,
};

class Sink {
 public:
  explicit Sink(Snapshot& out) : out_(out) {}

  static void onKeyCb(void* ctx, const char* key, size_t len) {
    static_cast<Sink*>(ctx)->key_ = keyFrom(key, len);
  }
  static void onStringCb(void* ctx, const char* value, size_t len) {
    static_cast<Sink*>(ctx)->onString(value, len);
  }
  static void onNumberCb(void* ctx, const char* value, size_t len) {
    static_cast<Sink*>(ctx)->onNumber(value, len);
  }
  static void onBoolCb(void* ctx, bool value) { static_cast<Sink*>(ctx)->onBool(value); }
  static void onNullCb(void* ctx) { static_cast<Sink*>(ctx)->onNull(); }
  static void onObjectStartCb(void* ctx) { static_cast<Sink*>(ctx)->openObject(); }
  static void onObjectEndCb(void* ctx) { static_cast<Sink*>(ctx)->closeContainer(); }
  static void onArrayStartCb(void* ctx) { static_cast<Sink*>(ctx)->openArray(); }
  static void onArrayEndCb(void* ctx) { static_cast<Sink*>(ctx)->closeContainer(); }

  bool valid() const { return rootSeen_ && !broken_ && depth_ == 0; }

 private:
  static constexpr int MAX_DEPTH = 32;

  Ctx top() const { return depth_ > 0 ? stack_[depth_ - 1] : Ctx::Ignore; }

  void push(Ctx ctx) {
    if (depth_ >= MAX_DEPTH) {
      broken_ = true;
      return;
    }
    stack_[depth_++] = ctx;
  }

  void openArray() {
    Ctx child = Ctx::Ignore;
    const Ctx parent = top();
    if (parent == Ctx::Root || parent == Ctx::Yandex) {
      if (key_ == Key::Received) child = Ctx::Received;
      else if (key_ == Key::Pending) child = Ctx::Pending;
    }
    push(child);
    key_ = Key::None;
  }

  void openObject() {
    Ctx child = Ctx::Ignore;
    const Ctx parent = top();
    if (depth_ == 0) {
      child = Ctx::Root;
      rootSeen_ = true;
    } else if (parent == Ctx::Root && key_ == Key::Yandex) {
      child = Ctx::Yandex;
    } else if ((parent == Ctx::Root || parent == Ctx::Yandex) && key_ == Key::HasMore) {
      child = Ctx::HasMore;
    } else if (parent == Ctx::Received || parent == Ctx::Pending) {
      child = Ctx::Award;
      award_ = Award{};
      awardSection_ = parent == Ctx::Pending ? 2 : 1;
    } else if (parent == Ctx::Award && key_ == Key::Percent) {
      child = Ctx::Percent;
    } else if (parent == Ctx::Award && key_ == Key::Progress) {
      child = Ctx::Progress;
    } else if (parent == Ctx::Progress && key_ == Key::Current) {
      child = Ctx::Current;
    } else if (parent == Ctx::Progress && key_ == Key::Target) {
      child = Ctx::Target;
    }
    push(child);
    key_ = Key::None;
  }

  void closeContainer() {
    if (depth_ == 0) {
      broken_ = true;
      return;
    }
    if (top() == Ctx::Award) finishAward();
    --depth_;
    key_ = Key::None;
  }

  void finishAward() {
    if (awardSection_ == 0) return;
    const uint8_t section = awardSection_;
    awardSection_ = 0;
    if (award_.id[0] == '\0' && award_.title[0] == '\0') return;
    if (section == 1) {
      if (out_.receivedCount < MAX_RECEIVED) {
        out_.received[out_.receivedCount++] = award_;
      } else {
        out_.truncated = true;
      }
    } else if (out_.pendingCount < MAX_PENDING) {
      out_.pending[out_.pendingCount++] = award_;
    } else {
      out_.truncated = true;
    }
  }

  void onString(const char* value, size_t len) {
    const Ctx ctx = top();
    if (ctx == Ctx::Award) {
      switch (key_) {
        case Key::Id:
          copyUtf8(award_.id, ID_LEN, value, len);
          break;
        case Key::Title:
          copyUtf8(award_.title, TITLE_LEN, value, len);
          break;
        case Key::Description:
          copyUtf8(award_.description, DESC_LEN, value, len);
          break;
        case Key::EarnedAt:
          award_.earnedAt = parseTimestamp(value, len);
          award_.earnedKnown = award_.earnedAt > 0;
          break;
        case Key::FirstObservedAt:
          award_.observedAt = parseTimestamp(value, len);
          break;
        default:
          break;
      }
    } else if ((ctx == Ctx::Root || ctx == Ctx::Yandex) && key_ == Key::CheckedAt) {
      out_.checkedAt = parseTimestamp(value, len);
    }
    key_ = Key::None;
  }

  void onNumber(const char* value, size_t len) {
    const Ctx ctx = top();
    if (ctx == Ctx::Award) {
      switch (key_) {
        case Key::Percent:
          award_.percent = clampPercent(toDouble(value, len));
          break;
        case Key::Progress:
          award_.progress = clampCount(toDouble(value, len));
          break;
        case Key::EarnedAt:
          award_.earnedAt = parseTimestamp(value, len);
          award_.earnedKnown = award_.earnedAt > 0;
          break;
        case Key::FirstObservedAt:
          award_.observedAt = parseTimestamp(value, len);
          break;
        default:
          break;
      }
    } else if (ctx == Ctx::Percent) {
      if (key_ == Key::Value) award_.percent = clampPercent(toDouble(value, len));
    } else if (ctx == Ctx::Current) {
      if (key_ == Key::Value) award_.progress = clampCount(toDouble(value, len));
    } else if (ctx == Ctx::Target) {
      if (key_ == Key::Value) award_.target = clampCount(toDouble(value, len));
    } else if (ctx == Ctx::HasMore) {
      const bool more = toDouble(value, len) > 0;
      if (key_ == Key::Received) out_.hasMoreReceived = more;
      else if (key_ == Key::Pending) out_.hasMorePending = more;
    } else if ((ctx == Ctx::Root || ctx == Ctx::Yandex) && key_ == Key::CheckedAt) {
      out_.checkedAt = parseTimestamp(value, len);
    }
    key_ = Key::None;
  }

  void onBool(bool value) {
    if (top() == Ctx::HasMore) {
      if (key_ == Key::Received) out_.hasMoreReceived = value;
      else if (key_ == Key::Pending) out_.hasMorePending = value;
    }
    key_ = Key::None;
  }

  void onNull() {
    if (top() == Ctx::Award) {
      if (key_ == Key::EarnedAt) {
        award_.earnedKnown = false;
        award_.earnedAt = 0;
      } else if (key_ == Key::FirstObservedAt) {
        award_.observedAt = 0;
      } else if (key_ == Key::Percent) {
        award_.percent = -1;
      } else if (key_ == Key::Progress) {
        award_.progress = -1;
        award_.target = -1;
      }
    }
    key_ = Key::None;
  }

  Snapshot& out_;
  Ctx stack_[MAX_DEPTH] = {};
  int depth_ = 0;
  Key key_ = Key::None;
  Award award_{};
  uint8_t awardSection_ = 0;
  bool rootSeen_ = false;
  bool broken_ = false;
};

}  // namespace

int64_t parseTimestamp(const char* text, size_t len) {
  if (!text || len == 0 || len >= 40) return 0;
  char buf[40];
  memcpy(buf, text, len);
  buf[len] = '\0';
  char* end = nullptr;
  const long long numeric = strtoll(buf, &end, 10);
  if (end && end != buf && *end == '\0') return numeric > 0 ? numeric : 0;
  const int64_t iso = isoToEpoch(text, len);
  return iso > 0 ? iso : 0;
}

bool parse(const char* json, size_t len, Snapshot& out) {
  out = Snapshot{};
  if (!json) return false;
  Sink sink(out);
  const JsonCallbacks callbacks{&sink,        &Sink::onKeyCb,     &Sink::onStringCb,
                                &Sink::onNumberCb, &Sink::onBoolCb, &Sink::onNullCb,
                                &Sink::onObjectStartCb, &Sink::onObjectEndCb,
                                &Sink::onArrayStartCb, &Sink::onArrayEndCb};
  StreamingJsonParser parser(callbacks);
  parser.feed(json, len);
  if (parser.hasError() || !sink.valid()) {
    out = Snapshot{};
    return false;
  }
  return true;
}

bool formatDate(int64_t epoch, char* out, size_t size) {
  if (epoch <= 0 || !out || size < 11) return false;
  const time_t stamp = static_cast<time_t>(epoch);
  struct tm local {};
  if (!localtime_r(&stamp, &local)) return false;
  snprintf(out, size, "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return true;
}

bool formatDateTime(int64_t epoch, char* out, size_t size) {
  if (epoch <= 0 || !out || size < 17) return false;
  const time_t stamp = static_cast<time_t>(epoch);
  struct tm local {};
  if (!localtime_r(&stamp, &local)) return false;
  snprintf(out, size, "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
           local.tm_hour, local.tm_min);
  return true;
}

}  // namespace ybxach
