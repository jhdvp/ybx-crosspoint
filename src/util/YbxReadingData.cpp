#include "YbxReadingData.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "PluginEventDeadline.h"
#include "PluginHttp.h"
#include "PluginLocations.h"
#include "PluginQueueStorage.h"

namespace {
struct CacheFiles {
  bool exists(const std::string& p) { return Storage.exists(p.c_str()); }
  bool remove(const std::string& p) { return Storage.remove(p.c_str()); }
  bool rename(const std::string& a, const std::string& b) { return Storage.rename(a.c_str(), b.c_str()); }
  bool writeComplete(const std::string& p, std::string_view text) {
    HalFile file;
    if (!Storage.openFileForWrite("YBX", p, file)) return false;
    const bool complete = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
    const bool closed = file.close();
    return complete && closed;
  }
};
struct Connection {
  std::string dir, token, binding;
  pluginhttp::RequestSpec request;
};

ybx::ReadingResult connection(Connection& out, const char* resource = "reading_summary") {
  if (!SETTINGS.pluginsEnabled) return ybx::ReadingResult::NotConnected;
  out.dir = PluginLocations::findPluginDir("ybx");
  if (out.dir.empty()) return ybx::ReadingResult::NotConnected;
  std::string manifest;
  if (!Storage.readFileToString("YBX", out.dir + "/device.json", 8192, manifest))
    return ybx::ReadingResult::ManifestUnsupported;
  JsonDocument filter;
  filter["token"] = true;
  filter["ybx"][resource] = true;
  JsonDocument doc;
  if (deserializeJson(doc, manifest, DeserializationOption::Filter(filter)))
    return ybx::ReadingResult::ManifestUnsupported;
  const std::string tokenFile = pluginhttp::inPluginDir(out.dir, doc["token"]["file"] | "");
  if (!pluginhttp::loadTokenFromFile(tokenFile, doc["token"]["path"] | "token", out.token) || out.token.empty())
    return ybx::ReadingResult::NotConnected;
  uint8_t hash[32];
  if (mbedtls_sha256(reinterpret_cast<const uint8_t*>(out.token.data()), out.token.size(), hash, 0) != 0)
    return ybx::ReadingResult::NotConnected;
  char hex[65];
  for (size_t i = 0; i < sizeof(hash); ++i) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  out.binding = hex;
  pluginhttp::readRequest(doc["ybx"][resource], "GET", out.request);
  if (out.request.method != "GET" || out.request.url.compare(0, 8, "https://") != 0)
    return ybx::ReadingResult::ManifestUnsupported;
  return ybx::ReadingResult::Ok;
}
void nullable(JsonVariant value, int32_t number) {
  if (number < 0)
    value.set(nullptr);
  else
    value.set(number);
}
}  // namespace

namespace ybx {
ReadingResult readingConnectionState() {
  Connection conn;
  return connection(conn);
}
bool loadReadingSummary(ReadingSummary& out) {
  Connection conn;
  if (connection(conn) != ReadingResult::Ok) return false;
  const std::string path = conn.dir + "/reading-summary.json";
  CacheFiles files;
  if (!pluginevents::recoverQueue(files, path)) return false;
  std::string raw;
  if (!Storage.readFileToString("YBX", path, 4096, raw)) return false;
  return parseCachedReadingSummary(raw, conn.binding, out);
}

ReadingResult refreshReadingSummary(ReadingSummary& out) {
  Connection conn;
  const auto ready = connection(conn);
  if (ready != ReadingResult::Ok) return ready;
  pluginhttp::substituteAll(conn.request.url, "{token}", conn.token);
  for (auto& header : conn.request.headers) pluginhttp::substituteAll(header.second, "{token}", conn.token);
  const pluginevents::EventDeadline deadline(millis(), 12000);
  String response;
  const int status = pluginhttp::request(
      nullptr, conn.request.url, "GET", "", conn.request.headers, response, 8192, nullptr,
      [&] { return deadline.remaining(millis()) == 0; }, 12000);
  if (status == 401 || status == 403) return ReadingResult::NotConnected;
  if (status < 200 || status >= 300) return ReadingResult::NetworkError;
  if (!parseReadingSummary(std::string_view(response.c_str(), response.length()), out))
    return ReadingResult::InvalidData;
  out.fetchedAt = trustedtime::trustedNow();
  JsonDocument doc;
  {
    JsonDocument filter;
    filter["week_days"][0]["date"] = true;
    filter["week_days"][0]["minutes"] = true;
    filter["week_days"][0]["sources"] = true;
    if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) return ReadingResult::InvalidData;
  }
  response.remove(0);
  doc["schema"] = 1;
  doc["fetched_at"] = out.fetchedAt;
  doc["binding"] = conn.binding;
  doc["today"] = out.date.data();
  doc["timezone"] = out.timezone.data();
  nullable(doc["today_minutes"]["value"], out.todayMinutes);
  nullable(doc["week_minutes"]["value"], out.weekMinutes);
  nullable(doc["current_streak_days"]["value"], out.streakDays);
  nullable(doc["books_finished_this_year"]["value"], out.finishedBooks);
  std::string serialized;
  serializeJson(doc, serialized);
  if (serialized.size() > 4096) return ReadingResult::StorageError;
  const std::string path = conn.dir + "/reading-summary.json";
  CacheFiles files;
  return pluginevents::replaceQueue(files, path, serialized) ? ReadingResult::Ok : ReadingResult::StorageError;
}

namespace {
bool readAwards(HalFile& file, AwardsPage& out, AwardSection section, size_t offset) {
  auto parser = makeUniqueNoThrow<AwardsParser>(out, section, offset);
  if (!parser) return false;
  char block[128];
  int read = 0;
  while ((read = file.read(block, sizeof(block))) > 0) parser->feed(block, read);
  return read == 0 && parser->finish();
}
}  // namespace
ReadingResult awardsConnectionState() {
  Connection conn;
  return connection(conn, "achievements");
}
bool loadAwardsPage(AwardsPage& out, AwardSection section, size_t offset) {
  Connection conn;
  if (connection(conn, "achievements") != ReadingResult::Ok) return false;
  const std::string path = conn.dir + "/achievements.json";
  CacheFiles files;
  if (!pluginevents::recoverQueue(files, path)) return false;
  HalFile file = Storage.open(path.c_str());
  if (!file || file.fileSize() > AWARDS_MAX_BYTES + 65) return false;
  char binding[65];
  if (file.read(binding, 65) != 65 || !awardsCacheBinding(std::string_view(binding, 65), conn.binding)) return false;
  return readAwards(file, out, section, offset);
}
ReadingResult refreshAwardsPage(AwardsPage& out, AwardSection section, size_t offset) {
  Connection conn;
  const auto ready = connection(conn, "achievements");
  if (ready != ReadingResult::Ok) return ready;
  pluginhttp::substituteAll(conn.request.url, "{token}", conn.token);
  for (auto& header : conn.request.headers) pluginhttp::substituteAll(header.second, "{token}", conn.token);
  const std::string path = conn.dir + "/achievements.json";
  const std::string response = path + ".response";
  const pluginevents::EventDeadline deadline(millis(), 12000);
  const int status = pluginhttp::requestToFile(
      nullptr, conn.request.url, "GET", "", conn.request.headers, response.c_str(), AWARDS_MAX_BYTES,
      [&] { return deadline.remaining(millis()) == 0; }, 12000);
  if (status < 200 || status >= 300) {
    Storage.remove(response.c_str());
    return status == 401 || status == 403 ? ReadingResult::NotConnected : ReadingResult::NetworkError;
  }
  bool parsed = false;
  {
    HalFile source = Storage.open(response.c_str());
    if (source) parsed = readAwards(source, out, section, offset);
  }
  if (!parsed) {
    Storage.remove(response.c_str());
    return ReadingResult::InvalidData;
  }
  CacheFiles files;
  const bool saved = pluginevents::replaceQueueWithWriter(files, path, [&](const std::string& pending) {
    HalFile source = Storage.open(response.c_str());
    HalFile dest;
    if (!source || !Storage.openFileForWrite("YBX", pending, dest)) return false;
    bool ok =
        dest.write(reinterpret_cast<const uint8_t*>(conn.binding.data()), conn.binding.size()) == conn.binding.size() &&
        dest.write('\n') == 1;
    const size_t expected = source.fileSize();
    size_t copied = 0;
    if (expected > AWARDS_MAX_BYTES) ok = false;
    uint8_t buffer[128];
    int count = 0;
    while (ok && (count = source.read(buffer, sizeof(buffer))) > 0) {
      const size_t bytes = static_cast<size_t>(count);
      ok = copied <= expected && bytes <= expected - copied && dest.write(buffer, bytes) == bytes;
      if (ok) copied += bytes;
    }
    const bool closed = dest.close();
    return ok && count == 0 && copied == expected && closed;
  });
  Storage.remove(response.c_str());
  return saved ? ReadingResult::Ok : ReadingResult::StorageError;
}
}  // namespace ybx
