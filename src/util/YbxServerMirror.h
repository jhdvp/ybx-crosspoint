#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Native port of the YBX plugin 1.6.17 server mirror.
//
// Three host families, each with a Europe and a Portugal mirror. Auto probes
// main -> eu -> pt and remembers the host that answered; a manual choice pins
// exactly one host and never probes another, so a connection failure is
// returned to the caller and the human changes the mode.
//
// Only connection failures switch hosts. An HTTP reply (including 401, 429 and
// 5xx) and a response-body error stay on the host that produced them: those are
// answers from a reachable server, not evidence that another mirror is better.
//
// Kept free of Arduino/ESP-IDF and of storage so the host suite
// (test/ybx_server_mirror) drives the same rules the firmware uses.
namespace ybxmirror {

enum class Mode : uint8_t { Auto = 0, Main = 1, Eu = 2, Pt = 3 };
constexpr uint8_t MODE_COUNT = 4;

// Host slots within a family, in Auto probe order.
constexpr uint8_t SLOT_MAIN = 0;
constexpr uint8_t SLOT_EU = 1;
constexpr uint8_t SLOT_PT = 2;
constexpr uint8_t SLOT_COUNT = 3;

// How long a mirror that answered stays preferred before Auto probes main
// again. Also the throttle after every host failed, so an unreachable service
// is not probed on every request.
constexpr uint32_t RETRY_WINDOW_MS = 5u * 60u * 1000u;

inline constexpr const char* FAMILY_HOSTS[][SLOT_COUNT] = {
    {"my.ybx.app", "emy.ybx.app", "pmy.ybx.app"},
    {"opds.ybx.app", "eopds.ybx.app", "popds.ybx.app"},
    {"sync.ybx.app", "esync.ybx.app", "psync.ybx.app"},
};
constexpr size_t FAMILY_COUNT = sizeof(FAMILY_HOSTS) / sizeof(FAMILY_HOSTS[0]);

// True when `left` is still ahead of `now` on the 32-bit millis() clock.
inline bool before(uint32_t now, uint32_t left) { return static_cast<int32_t>(left - now) > 0; }

// Family index for a main or mirror host, or -1 for anything else.
inline int familyForHost(std::string_view host) {
  for (size_t f = 0; f < FAMILY_COUNT; f++) {
    for (uint8_t s = 0; s < SLOT_COUNT; s++) {
      if (host == FAMILY_HOSTS[f][s]) return static_cast<int>(f);
    }
  }
  return -1;
}

// Slot of a host within its family, or SLOT_COUNT when it is not one.
inline uint8_t slotForHost(int family, std::string_view host) {
  if (family < 0) return SLOT_COUNT;
  for (uint8_t s = 0; s < SLOT_COUNT; s++) {
    if (host == FAMILY_HOSTS[family][s]) return s;
  }
  return SLOT_COUNT;
}

// The host a manual mode pins. Auto has no pinned host.
inline uint8_t pinnedSlot(Mode mode) {
  return mode == Mode::Auto ? SLOT_COUNT : static_cast<uint8_t>(static_cast<int>(mode) - 1);
}

// A plain absolute http(s) URL split at its host. Userinfo and an explicit port
// disqualify mirroring, as they do in the plugin: such an address is not one of
// the YBX service endpoints and must reach the caller unchanged.
struct Url {
  bool mirrorable = false;
  size_t hostBegin = 0;
  size_t hostEnd = 0;
  std::string_view scheme;
  std::string_view host;
};

inline Url parseUrl(std::string_view url) {
  Url out;
  const size_t sep = url.find("://");
  if (sep == std::string_view::npos) return out;
  out.scheme = url.substr(0, sep);
  if (out.scheme != "http" && out.scheme != "https") return out;
  const size_t begin = sep + 3;
  size_t end = begin;
  while (end < url.size() && url[end] != '/' && url[end] != '?' && url[end] != '#') end++;
  const std::string_view authority = url.substr(begin, end - begin);
  if (authority.empty() || authority.find('@') != std::string_view::npos ||
      authority.find(':') != std::string_view::npos) {
    return out;
  }
  out.hostBegin = begin;
  out.hostEnd = end;
  out.host = authority;
  out.mirrorable = familyForHost(authority) >= 0;
  return out;
}

// Same URL with another host: scheme, path, query and fragment are preserved.
inline std::string withHost(std::string_view url, const Url& parsed, std::string_view host) {
  std::string out;
  out.reserve(url.size() + host.size());
  out.append(url.substr(0, parsed.hostBegin));
  out.append(host);
  out.append(url.substr(parsed.hostEnd));
  return out;
}

// What Auto remembers for one scheme+family: the host that answered and when
// it is due for a main probe again. retryAt == 0 means "probe main first".
struct Preference {
  uint8_t slot = SLOT_MAIN;
  uint32_t retryAt = 0;
};

// The host Auto probes first: the remembered one while its window is open.
inline uint8_t firstSlot(const Preference& pref, uint32_t now) {
  return pref.retryAt != 0 && before(now, pref.retryAt) ? pref.slot : SLOT_MAIN;
}

// Probe order for one request. Returns the count written to `out`.
// A manual mode yields exactly one host; Auto yields the first slot followed
// by the rest in main -> eu -> pt order.
inline uint8_t candidates(Mode mode, const Preference& pref, uint32_t now, uint8_t (&out)[SLOT_COUNT]) {
  const uint8_t pinned = pinnedSlot(mode);
  if (pinned != SLOT_COUNT) {
    out[0] = pinned;
    return 1;
  }
  const uint8_t first = firstSlot(pref, now);
  uint8_t n = 0;
  out[n++] = first;
  for (uint8_t s = 0; s < SLOT_COUNT; s++) {
    if (s != first) out[n++] = s;
  }
  return n;
}

// After a host answered. Main clears the window so the next request probes it
// first; a mirror keeps the later of the existing window and a fresh one, so a
// run of successful mirror requests does not shorten the throttle.
inline Preference afterSuccess(uint8_t slot, const Preference& prev, uint32_t now) {
  Preference next;
  next.slot = slot;
  if (slot == SLOT_MAIN) {
    next.retryAt = 0;
  } else {
    const uint32_t fresh = now + RETRY_WINDOW_MS;
    next.retryAt = prev.retryAt != 0 && before(now, prev.retryAt) ? prev.retryAt : fresh;
  }
  return next;
}

// After every candidate failed: keep probing a mirror, not main, for one
// window, so an unreachable service is not retried on every request.
inline Preference afterAllFailed(uint8_t first, uint32_t now) {
  Preference next;
  next.slot = first == SLOT_MAIN ? SLOT_EU : first;
  next.retryAt = now + RETRY_WINDOW_MS;
  return next;
}

// The persisted server choice. Auto is the default; a manual mode pins one host.
Mode currentMode();
// Stores the choice and forgets the remembered hosts, so Auto starts from main
// again. Changing the mode is a human decision, not a probe result.
void setMode(Mode mode);
const char* modeName(Mode mode);

// One request's probe order. A non-YBX URL, or one carrying userinfo or a port,
// yields a single candidate equal to the original address, so callers can run
// the same loop for every request.
class Plan {
 public:
  explicit Plan(const std::string& url);
  uint8_t count() const { return count_; }
  // Candidate address; index 0 is the original URL when nothing is mirrored.
  std::string urlAt(uint8_t index) const;
  // Report the outcome so the next request probes the right host first.
  // `answered` means the host produced an HTTP reply or a body error: either
  // way it is reachable, so Auto keeps it and stops probing.
  void answered(uint8_t index) const;
  void allFailed() const;

 private:
  std::string url_;
  Url parsed_;
  int family_ = -1;
  uint8_t schemeIndex_ = 0;
  uint8_t slots_[SLOT_COUNT] = {SLOT_MAIN, SLOT_EU, SLOT_PT};
  uint8_t count_ = 1;
  uint8_t first_ = SLOT_MAIN;
};

}  // namespace ybxmirror