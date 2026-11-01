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

// True when a clock gate may start a blocking SNTP attempt. Inside a
// NoBlockingClockSync window it is false: the gate uses the clock as it is,
// so an HTTPS handler in a sleep drain fails immediately instead of adding a
// multi-second sync to the sleep latency (queued events retry on the next
// drain).
bool clockSyncAllowed();

// Restores SNTP blocking for the clock gate when it goes out of scope.
class NoBlockingClockSync {
 public:
  NoBlockingClockSync();
  ~NoBlockingClockSync();
  NoBlockingClockSync(const NoBlockingClockSync&) = delete;
  NoBlockingClockSync& operator=(const NoBlockingClockSync&) = delete;
};

// The blocking gate for one operation: plain-http URLs pass (they carry no
// certificate dates), https URLs require a trustworthy clock, syncing SNTP
// once if needed (skipped while clockSyncAllowed() is false). False means the
// caller must stop: the request was not attempted and Failure::Clock was
// recorded. This call also starts a fresh failure record.
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
// when the URL is not https or the clock is unusable. Starts a fresh failure
// record like ensureClockFor().
bool prepareHttpsClient(freeink::SecureHttpClient& client, const std::string& url);

// Maps the transport's connect failure to the UI-facing classification.
Failure failureFromConnect(freeink::SecureClient::ConnectFailure failure);

// The most recent trust failure from any request path, for the UI to explain
// a failure right after it happened. ensureClockFor() and
// prepareHttpsClient() clear it first, so every network operation reports its
// own outcome; a non-transport flow that reads lastFailure() (e.g. a failed
// token-file load) must clearFailure() at its start so a stale failure cannot
// describe a new error. Only trust failures are kept.
void clearFailure();
void recordFailure(Failure failure);
Failure lastFailure();

}  // namespace tlspolicy
