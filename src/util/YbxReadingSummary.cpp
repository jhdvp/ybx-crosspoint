#include "YbxReadingSummary.h"

#include <ArduinoJson.h>

#include <cstring>
#include <limits>

namespace ybx {
namespace {
int32_t metric(JsonVariantConst value) {
  if (!value.is<int64_t>()) return -1;
  const int64_t number = value.as<int64_t>();
  return number >= 0 && number <= std::numeric_limits<int32_t>::max() ? static_cast<int32_t>(number) : -1;
}
}  // namespace

bool parseReadingSummary(const std::string_view json, ReadingSummary& out) {
  if (json.empty() || json.size() > 8192) return false;
  JsonDocument filter;
  filter["today"] = true;
  filter["timezone"] = true;
  filter["today_minutes"]["value"] = true;
  filter["week_minutes"]["value"] = true;
  filter["current_streak_days"]["value"] = true;
  filter["books_finished_this_year"]["value"] = true;
  filter["week_days"][0]["date"] = true;
  filter["week_days"][0]["minutes"] = true;
  filter["week_days"][0]["sources"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json.data(), json.size(), DeserializationOption::Filter(filter))) return false;
  const char* date = doc["today"] | "";
  const int32_t today = civilDay(date);
  const char* zone = doc["timezone"] | "";
  if (today < 0 || !zone[0] || std::strlen(zone) >= out.timezone.size()) return false;
  const int32_t monday = today - (today + 3) % 7;
  uint8_t seen = 0;
  for (JsonObjectConst day : doc["week_days"].as<JsonArrayConst>()) {
    const int32_t ordinal = civilDay(day["date"] | "");
    if (ordinal < monday || ordinal > today) return false;
    const auto index = static_cast<size_t>(ordinal - monday);
    if (seen & (1u << index)) return false;
    seen |= 1u << index;
  }
  out = {};
  std::memcpy(out.date.data(), date, 10);
  std::strcpy(out.timezone.data(), zone);
  out.todayMinutes = metric(doc["today_minutes"]["value"]);
  out.weekMinutes = metric(doc["week_minutes"]["value"]);
  out.streakDays = metric(doc["current_streak_days"]["value"]);
  out.finishedBooks = metric(doc["books_finished_this_year"]["value"]);
  for (JsonObjectConst day : doc["week_days"].as<JsonArrayConst>()) {
    const size_t index = civilDay(day["date"] | "") - monday;
    if (day["sources"].as<JsonArrayConst>().size() > 0) out.days[index] = metric(day["minutes"]);
  }
  out.available = true;
  return true;
}
bool parseCachedReadingSummary(std::string_view json, std::string_view binding, ReadingSummary& out) {
  if (json.size() > 4096 || binding.empty()) return false;
  int64_t fetchedAt = 0;
  {
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) || (doc["schema"] | 0) != 1 || binding != (doc["binding"] | ""))
      return false;
    const int64_t saved = doc["fetched_at"] | int64_t{0};
    if (saved >= 1735689600LL && saved <= 4133980800LL) fetchedAt = saved;
  }
  if (!parseReadingSummary(json, out)) return false;
  out.fetchedAt = fetchedAt;
  return true;
}
}  // namespace ybx
