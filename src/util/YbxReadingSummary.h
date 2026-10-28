#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ybx {
struct ReadingSummary {
  std::array<char, 11> date{};
  std::array<char, 64> timezone{};
  int32_t todayMinutes = -1;
  int32_t weekMinutes = -1;
  int32_t streakDays = -1;
  int32_t finishedBooks = -1;
  std::array<int32_t, 7> days{{-1, -1, -1, -1, -1, -1, -1}};
  int64_t fetchedAt = 0;
  bool available = false;
};

// Account-local civil dates, independent of the reader's clock/timezone.
inline int32_t civilDay(std::string_view date) {
  if (date.size() != 10 || date[4] != '-' || date[7] != '-') return -1;
  for (size_t i = 0; i < date.size(); ++i)
    if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return -1;
  const int year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + date[3] - '0';
  const int month = (date[5] - '0') * 10 + date[6] - '0';
  const int day = (date[8] - '0') * 10 + date[9] - '0';
  if (year < 1970 || year > 2199 || month < 1 || month > 12) return -1;
  const auto leap = [](int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
  constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day < 1 || day > lengths[month - 1] + (month == 2 && leap(year))) return -1;
  int32_t result = day - 1;
  for (int y = 1970; y < year; ++y) result += 365 + leap(y);
  for (int m = 1; m < month; ++m) result += lengths[m - 1] + (m == 2 && leap(year));
  return result;
}
bool parseReadingSummary(std::string_view json, ReadingSummary& out);
bool parseCachedReadingSummary(std::string_view json, std::string_view binding, ReadingSummary& out);
}  // namespace ybx
