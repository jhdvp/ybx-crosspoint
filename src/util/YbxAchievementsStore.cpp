#include "YbxAchievementsStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <cstring>

#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

namespace ybxach {
namespace {

// Mirrors the shipped YBX device.json (token.file / token.path). The endpoint
// also lives outside that manifest, because the native screen replaces the
// plugin catalog rather than driving it.
constexpr char TOKEN_FILE[] = "token.json";
constexpr char TOKEN_PATH[] = "access_token";
// The service also accepts limit up to 50, but the RAM model caps each section
// at MAX_RECEIVED / MAX_PENDING, so one 20-card page is the whole screen.
constexpr char API_URL[] = "https://my.ybx.app/api/v1/crosspoint/achievements?limit=20";

constexpr char MAGIC[4] = {'Y', 'B', 'A', 'C'};
constexpr uint8_t CACHE_VERSION = 1;
// magic(4) version(1) reserved(3) payload size(4, LE) token fingerprint(8, LE)
constexpr size_t HEADER_LEN = 20;

// 40 cards of bounded title/description plus JSON keys stays under 20 KB; the
// cap also bounds the transient heap while the request runs.
constexpr size_t MAX_RESPONSE = 24 * 1024;

void putLe32(uint8_t* p, const uint32_t value) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

void putLe64(uint8_t* p, const uint64_t value) {
  for (int i = 0; i < 8; i++) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t getLe32(const uint8_t* p) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) value |= static_cast<uint32_t>(p[i]) << (8 * i);
  return value;
}

uint64_t getLe64(const uint8_t* p) {
  uint64_t value = 0;
  for (int i = 0; i < 8; i++) value |= static_cast<uint64_t>(p[i]) << (8 * i);
  return value;
}

uint64_t tokenFingerprint(const std::string& token) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char c : token) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool saveCached(const Snapshot& snapshot) {
  std::string token;
  if (!loadToken(token)) return false;
  Storage.mkdir("/.crosspoint/ybx", true);

  uint8_t header[HEADER_LEN] = {};
  memcpy(header, MAGIC, sizeof(MAGIC));
  header[4] = CACHE_VERSION;
  putLe32(header + 8, static_cast<uint32_t>(sizeof(Snapshot)));
  putLe64(header + 12, tokenFingerprint(token));

  const std::string tmp = std::string(CACHE_PATH) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("YBXA", tmp, file)) return false;
    const bool written = file.write(header, sizeof(header)) == sizeof(header) &&
                         file.write(&snapshot, sizeof(snapshot)) == sizeof(snapshot);
    file.flush();
    if (!written) {
      if (file.isOpen()) file.close();
      LOG_ERR("YBXA", "short cache write");
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(tmp.c_str(), CACHE_PATH)) {
    Storage.remove(tmp.c_str());
    LOG_ERR("YBXA", "cache replace failed");
    return false;
  }
  return true;
}

}  // namespace

bool loadToken(std::string& token) {
  token.clear();
  const std::string dir = PluginLocations::findPluginDir("ybx");
  if (dir.empty()) return false;
  return pluginhttp::loadTokenFromFile(pluginhttp::inPluginDir(dir, TOKEN_FILE), TOKEN_PATH, token);
}

bool loadCached(Snapshot& out) {
  std::string token;
  if (!loadToken(token)) return false;

  HalFile file;
  if (!Storage.openFileForRead("YBXA", CACHE_PATH, file)) return false;
  uint8_t header[HEADER_LEN];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  if (memcmp(header, MAGIC, sizeof(MAGIC)) != 0 || header[4] != CACHE_VERSION ||
      getLe32(header + 8) != static_cast<uint32_t>(sizeof(Snapshot)) ||
      getLe64(header + 12) != tokenFingerprint(token)) {
    return false;
  }
  if (file.read(&out, sizeof(out)) != static_cast<int>(sizeof(out))) return false;
  if (out.receivedCount > MAX_RECEIVED || out.pendingCount > MAX_PENDING) {
    out = Snapshot{};
    return false;
  }
  return true;
}

FetchStatus refresh(Snapshot& out, bool* dataKept) {
  if (dataKept) *dataKept = true;
  std::string token;
  if (!loadToken(token)) return FetchStatus::NoToken;

  pluginhttp::Headers headers;
  headers.emplace_back("Authorization", "Bearer " + token);
  String response;
  const int status = pluginhttp::request(nullptr, API_URL, "GET", "", headers, response, MAX_RESPONSE);

  if (status < 0) return FetchStatus::Offline;
  if (status == 401 || status == 403) return FetchStatus::Expired;
  if (status == 409) return FetchStatus::NotLinked;
  if (status == 429) return FetchStatus::RateLimited;
  if (status == 404) return FetchStatus::Disabled;
  if (status < 200 || status >= 300) {
    LOG_ERR("YBXA", "achievements HTTP %d", status);
    return FetchStatus::HttpError;
  }

  if (!parse(response.c_str(), response.length(), out)) {
    // parse() reset `out`; keep the last good view rather than presenting a
    // malformed body as an empty account.
    const bool restored = loadCached(out);
    if (!restored) out = Snapshot{};
    if (dataKept) *dataKept = restored;
    return FetchStatus::BadResponse;
  }
  out.fetchedAt = trustedtime::trustedNow();
  if (!saveCached(out)) LOG_ERR("YBXA", "achievements cache not saved");
  return FetchStatus::Ok;
}

}  // namespace ybxach
