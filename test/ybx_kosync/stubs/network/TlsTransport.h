#pragma once

#include <SecureHttpClient.h>

#include <string>

#include "network/TlsPolicy.h"

namespace fake {
inline bool allowVerifiedTls = true;
inline bool tlsPrepared = false;
inline unsigned tlsAttempts = 0;
}  // namespace fake

namespace tlspolicy {
inline bool prepareHttpsClient(freeink::SecureHttpClient& http, const std::string& url) {
  ++fake::tlsAttempts;
  if (!isHttpsUrl(url) || !fake::allowVerifiedTls) return false;
  http.setCACert("TEST TRUST ROOT");
  fake::tlsPrepared = true;
  return true;
}
}  // namespace tlspolicy
