#pragma once

#include <cctype>
#include <cstdint>
#include <string>

// Transport-security policy for every request the firmware makes on the
// user's behalf. Pure and header-only (no Arduino/wolfSSL headers) so the
// decisions are host-testable; the CA bundle, clock and transport wiring live
// in TlsPolicy.cpp.
namespace tlspolicy {

// Why a TLS-protected request could not be trusted, or was not attempted.
// Distinct from an ordinary network error so the UI can give an actionable
// message instead of "download failed".
enum class Failure : uint8_t {
  None,
  Clock,             // no trustworthy wall clock: certificate dates unverifiable
  Certificate,       // chain rejected: unknown CA, bad signature, expired
  Hostname,          // certificate does not cover the requested host
  InsecureRedirect,  // server redirected https to plain http; refused
  Configuration,     // trust configuration or TLS support missing
  Network,           // connect/transport failure, not a trust failure
};

// A certificate can only be validated against a plausible present-day clock.
// Below 2025-01-01 the clock was never set (cold boot before SNTP), and every
// chain would look expired or not yet valid. TrustedTime uses the same floor.
inline bool clockReady(const int64_t epochSeconds) { return epochSeconds >= 1735689600LL; }

// Runs the clock gate for one operation: when the clock is not usable, calls
// sync() once (SNTP on the device) and checks again. Returns false when the
// clock is still unusable, in which case a verified request must not start.
// now/sync are injected for host tests.
template <typename NowFn, typename SyncFn>
bool ensureClock(NowFn now, SyncFn sync) {
  if (clockReady(now())) return true;
  if (!sync()) return false;
  return clockReady(now());
}

// scheme://host:port, lowercased, with the scheme's default port made explicit
// and any userinfo dropped. Two URLs with equal origins may share credentials.
inline std::string originOf(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};
  std::string scheme = url.substr(0, schemeEnd);
  const size_t hostStart = schemeEnd + 3;
  const size_t hostEnd = url.find_first_of("/?#", hostStart);
  std::string authority =
      url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) authority.erase(0, at + 1);
  for (auto* part : {&scheme, &authority}) {
    for (char& c : *part) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  const size_t bracket = authority.rfind(']');  // IPv6 literal: its colons are not a port
  if (authority.find(':', bracket == std::string::npos ? 0 : bracket) == std::string::npos) {
    authority += scheme == "https" ? ":443" : ":80";
  }
  return scheme + "://" + authority;
}

inline bool isHttpsUrl(const std::string& url) { return originOf(url).rfind("https://", 0) == 0; }

// One followed redirect, as decided before the next request is issued.
struct RedirectDecision {
  bool follow = false;      // false: stop and report the 3xx instead
  bool sameOrigin = false;  // false: withhold credentials and caller headers
  bool downgrade = false;   // https -> http step-down
};

// Refuses an https -> http step-down unless the caller explicitly allows it:
// a redirect must not silently drop transport security. A hop that leaves the
// starting origin must not carry the caller's credentials.
inline RedirectDecision decideRedirect(const std::string& fromUrl, const std::string& toUrl,
                                       const bool allowDowngrade = false) {
  RedirectDecision decision;
  const std::string fromOrigin = originOf(fromUrl);
  const std::string toOrigin = originOf(toUrl);
  if (fromOrigin.empty() || toOrigin.empty()) return decision;  // unparseable: stop
  decision.downgrade = fromOrigin.rfind("https://", 0) == 0 && toOrigin.rfind("http://", 0) == 0;
  decision.sameOrigin = fromOrigin == toOrigin;
  decision.follow = !decision.downgrade || allowDowngrade;
  return decision;
}

inline bool isTrustFailure(const Failure failure) {
  return failure == Failure::Clock || failure == Failure::Certificate || failure == Failure::Hostname ||
         failure == Failure::InsecureRedirect || failure == Failure::Configuration;
}

}  // namespace tlspolicy
// format-pass-marker
