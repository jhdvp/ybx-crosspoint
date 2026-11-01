#pragma once

#include <cstdint>
#include <string>

#include "util/YbxAchievements.h"

// Transport and offline cache for the account's achievements. The YBX plugin's
// bearer token identifies the account, so the cache belongs to that token and
// is discarded when it changes (sign-out or another account).
namespace ybxach {

enum class FetchStatus : uint8_t {
  Ok,
  NoToken,      // YBX plugin absent or signed out
  Expired,      // 401/403: the token was rejected or lacks the catalog scope
  NotLinked,    // 409: no Yandex account is connected to this device
  RateLimited,  // 429
  Disabled,     // 404: the service does not expose the endpoint
  Offline,      // transport failure: no Wi-Fi, DNS, TLS, timeout, or oversize body
  HttpError,    // any other HTTP status
  BadResponse,  // 2xx whose body did not parse
};

// Token the native screens share with the YBX plugin (device.json token file).
bool loadToken(std::string& token);

// Cached snapshot for the current token. False when absent, unreadable, or
// written while a different token was signed in.
bool loadCached(Snapshot& out);

// One authenticated GET of the first page for both sections. On Ok this
// replaces `out` and the cache. On a 2xx body that fails to parse, `out` is
// restored from the cache (or reset) so a stale view is never mistaken for an
// empty account; then `dataKept` reports whether that restore succeeded and
// `out` holds a displayable snapshot. Any other failure leaves `out` untouched.
FetchStatus refresh(Snapshot& out, bool* dataKept = nullptr);

constexpr const char* CACHE_PATH = "/.crosspoint/ybx/achievements.bin";

}  // namespace ybxach
