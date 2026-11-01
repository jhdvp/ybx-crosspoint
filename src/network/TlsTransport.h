#pragma once

#include <SecureHttpClient.h>

#include <string>

#include "TlsPolicy.h"

// Firmware wiring between the pure TlsPolicy decisions and the wolfSSL
// transport: clock gate, CA bundle, and failure classification. Every request
// path that carries the reader token or downloads payloads goes through here.
namespace tlspolicy {

// The blocking clock gate: true when the system clock is plausible now,
// otherwise one bounded SNTP attempt. Shared by the URL-specific entry points.
bool ensureClock();

// The blocking gate for one operation: plain-http URLs pass (they carry no
// certificate dates), https URLs require a trustworthy clock, syncing SNTP
// once if needed. False means the caller must stop: the request was not
// attempted and Failure::Clock was recorded.
bool ensureClockFor(const std::string& url);

// Verified transport for https URLs: installs the firmware CA bundle, which
// makes the SDK check the peer chain and the host name. Plain-http URLs are
// left untouched (the SDK then uses its plain WiFiClient path).
void applyTrustFor(freeink::SecureHttpClient& client, const std::string& url);

// Installs the CA bundle unconditionally, for multi-hop transfers where the
// per-hop URL is not visible to the caller. The transport consults it only for
// https connections, so plain-http hops are unaffected.
void installTrust(freeink::SecureHttpClient& client);

// Strict variant for credential-bearing requests: https only, trustworthy
// clock required, verified trust installed. Returns false without connecting
// when the URL is not https or the clock is unusable.
bool prepareHttpsClient(freeink::SecureHttpClient& client, const std::string& url);

// Maps the transport's connect failure to the UI-facing classification.
Failure failureFromConnect(freeink::SecureClient::ConnectFailure failure);

// The most recent trust failure from any request path, for the UI to explain
// a failure right after it happened. Clear at the start of an operation so a
// stale failure cannot describe a new error; only trust failures are kept.
void clearFailure();
void recordFailure(Failure failure);
Failure lastFailure();

}  // namespace tlspolicy
