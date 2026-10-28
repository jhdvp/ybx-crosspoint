#include "TlsTransport.h"

#include <Logging.h>
#include <TrustedTime.h>

#include "TlsRoots.h"

namespace {
// Only paid when the clock is not set, once per operation: SNTP needs a few
// seconds on a weak link, and a longer wait would freeze the UI without
// improving the outcome.
constexpr uint32_t kClockSyncTimeoutMs = 15000;

tlspolicy::Failure g_lastFailure = tlspolicy::Failure::None;
}  // namespace

bool tlspolicy::ensureClock() {
  return ensureClock([] { return trustedtime::trustedNow(); },
                     [] { return trustedtime::syncNow(kClockSyncTimeoutMs); });
}

bool tlspolicy::ensureClockFor(const std::string& url) {
  if (!isHttpsUrl(url)) return true;
  if (ensureClock()) return true;
  LOG_ERR("TLS", "No trustworthy clock; refusing to verify a certificate against it");
  recordFailure(Failure::Clock);
  return false;
}

void tlspolicy::applyTrustFor(freeink::SecureHttpClient& client, const std::string& url) {
  if (isHttpsUrl(url)) client.setCACert(TLS_ROOTS_PEM);
}

void tlspolicy::installTrust(freeink::SecureHttpClient& client) { client.setCACert(TLS_ROOTS_PEM); }

bool tlspolicy::prepareHttpsClient(freeink::SecureHttpClient& client, const std::string& url) {
  if (!isHttpsUrl(url)) {
    LOG_ERR("TLS", "Refusing a non-https request on a credential-bearing path");
    recordFailure(Failure::Configuration);
    return false;
  }
  if (!ensureClock()) {
    LOG_ERR("TLS", "No trustworthy clock; refusing to verify a certificate against it");
    recordFailure(Failure::Clock);
    return false;
  }
  client.setCACert(TLS_ROOTS_PEM);
  return true;
}

tlspolicy::Failure tlspolicy::failureFromConnect(const freeink::SecureClient::ConnectFailure failure) {
  switch (failure) {
    case freeink::SecureClient::ConnectFailure::None:
    case freeink::SecureClient::ConnectFailure::Aborted:
      return Failure::None;
    case freeink::SecureClient::ConnectFailure::Clock:
      return Failure::Clock;
    case freeink::SecureClient::ConnectFailure::Certificate:
      return Failure::Certificate;
    case freeink::SecureClient::ConnectFailure::Hostname:
      return Failure::Hostname;
    case freeink::SecureClient::ConnectFailure::Configuration:
      return Failure::Configuration;
    case freeink::SecureClient::ConnectFailure::Transport:
    case freeink::SecureClient::ConnectFailure::Timeout:
    case freeink::SecureClient::ConnectFailure::Memory:
      return Failure::Network;
  }
  return Failure::Network;
}

void tlspolicy::clearFailure() { g_lastFailure = Failure::None; }

void tlspolicy::recordFailure(const Failure failure) {
  if (isTrustFailure(failure)) g_lastFailure = failure;
}

tlspolicy::Failure tlspolicy::lastFailure() { return g_lastFailure; }
// format-pass-marker
