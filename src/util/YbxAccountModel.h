#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Pure logic behind the native YBX account screen: which connected sources a
// /catalog/lists response describes, what an HTTP status means for the stored
// reader token, and the device-code polling rules. Header-only and free of
// Arduino dependencies so host tests can cover it.
namespace ybxaccount {

enum class SourceKind : uint8_t { Yandex = 0, Wb, Litres };
constexpr size_t SOURCE_COUNT = 3;

// A catalog list URL names the provider that published it. The aggregated
// endpoint returns one entry per shelf/feed, so the set of providers present
// in the response is exactly the set of connected sources.
enum class ListKind : uint8_t { None, Yandex, Wb, Litres };

inline ListKind classifyListUrl(std::string_view url) {
  // Catalog links describe connected providers only when the YBX service
  // issued them; an unrelated URL must never masquerade as a connected source.
  constexpr std::string_view prefix = "https://my.ybx.app/api/v1";
  if (url.substr(0, prefix.size()) != prefix) return ListKind::None;
  url.remove_prefix(prefix.size());
  const auto query = url.find('?');
  if (query != std::string_view::npos) url = url.substr(0, query);
  if (url == "/crosspoint/wb/catalog") return ListKind::Wb;
  if (url == "/crosspoint/litres/catalog") return ListKind::Litres;
  if (url == "/crosspoint/catalog") return ListKind::Yandex;
  return ListKind::None;
}

struct SourceSummary {
  uint16_t lists = 0;
};

struct Summary {
  std::array<SourceSummary, SOURCE_COUNT> sources{};
  uint16_t totalLists = 0;
  uint8_t connectedMask = 0;

  void clear() { *this = Summary{}; }

  void addList(ListKind kind) {
    if (kind == ListKind::None) return;
    const auto index = static_cast<size_t>(kind) - 1;
    auto& slot = sources[index];
    if (slot.lists != UINT16_MAX) ++slot.lists;
    if (totalLists != UINT16_MAX) ++totalLists;
    connectedMask = static_cast<uint8_t>(connectedMask | (1u << index));
  }

  bool connected(SourceKind kind) const {
    return (connectedMask & (1u << static_cast<uint8_t>(kind))) != 0;
  }

  uint16_t shelfCount(SourceKind kind) const { return sources[static_cast<size_t>(kind)].lists; }

  int connectedCount() const {
    int count = 0;
    for (size_t i = 0; i < SOURCE_COUNT; ++i)
      if (connectedMask & (1u << i)) ++count;
    return count;
  }
};

// What one /catalog/lists request says about the account. The reader token and
// a provider session are separate failures: the token is YBX's own bearer
// credential, while a provider session is the upstream Yandex/WB/LitRes login
// that only the web cabinet can repair.
enum class ListsOutcome : uint8_t {
  Ok,
  InvalidToken,    // 401/403 without X-YBX-Device-Removed: token expired/revoked
  DeviceRemoved,   // 401/403 with X-YBX-Device-Removed: this device was unlinked
  ProviderSession, // 409 books_consent_required: provider login needs repair
  Offline,         // transport failure (status < 0)
  Failed,
};

inline ListsOutcome listsOutcomeFromStatus(const int status, const bool deviceRemoved) {
  if (status >= 200 && status < 300) return ListsOutcome::Ok;
  if (status == 401 || status == 403)
    return deviceRemoved ? ListsOutcome::DeviceRemoved : ListsOutcome::InvalidToken;
  if (status == 409) return ListsOutcome::ProviderSession;
  if (status < 0) return ListsOutcome::Offline;
  return ListsOutcome::Failed;
}

inline bool isProviderSessionBody(std::string_view code) {
  return code == "books_consent_required";
}

inline bool headerIsTrue(std::string_view value) {
  return value == "true" || value == "True" || value == "1";
}

constexpr unsigned long MIN_POLL_INTERVAL_SECONDS = 3;
constexpr unsigned long MIN_CODE_LIFETIME_SECONDS = 60;
constexpr unsigned long SLOW_DOWN_STEP_MS = 5000;
constexpr long MAX_SAFE_DURATION_SECONDS = 3600;

inline unsigned long clampPollIntervalMs(const long seconds) {
  const long clamped = seconds < static_cast<long>(MIN_POLL_INTERVAL_SECONDS)
                           ? static_cast<long>(MIN_POLL_INTERVAL_SECONDS)
                           : seconds > MAX_SAFE_DURATION_SECONDS ? MAX_SAFE_DURATION_SECONDS : seconds;
  return static_cast<unsigned long>(clamped) * 1000UL;
}

inline unsigned long clampCodeLifetimeMs(const long seconds) {
  const long clamped = seconds < static_cast<long>(MIN_CODE_LIFETIME_SECONDS)
                           ? static_cast<long>(MIN_CODE_LIFETIME_SECONDS)
                           : seconds > MAX_SAFE_DURATION_SECONDS ? MAX_SAFE_DURATION_SECONDS : seconds;
  return static_cast<unsigned long>(clamped) * 1000UL;
}

inline bool deadlineReached(const unsigned long nowMs, const unsigned long deadlineMs) {
  return static_cast<int32_t>(static_cast<uint32_t>(nowMs - deadlineMs)) >= 0;
}

inline unsigned long slowedDownIntervalMs(const unsigned long intervalMs) {
  return intervalMs + SLOW_DOWN_STEP_MS;
}

enum class PollOutcome : uint8_t { Pending, SlowDown, Expired, Denied };

inline PollOutcome pollOutcomeFromErrorCode(std::string_view code) {
  if (code == "slow_down") return PollOutcome::SlowDown;
  if (code == "expired_token") return PollOutcome::Expired;
  if (code == "access_denied") return PollOutcome::Denied;
  return PollOutcome::Pending;
}

// Origin of the web cabinet that manages providers, derived from the manifest's
// own catalog host so self-hosted and preview deployments point at their
// cabinet instead of a hard-coded domain. Writes "https://<host>/#sources";
// rejects a non-HTTPS or unusable origin, so the QR never offers a downgrade.
inline bool cabinetUrlFromListsUrl(const std::string_view url, char* out, const size_t size) {
  constexpr std::string_view PREFIX = "https://";
  constexpr std::string_view SUFFIX = "/#sources";
  constexpr size_t MAX_HOST_LENGTH = 63;  // QR payloads stay small on a 2.9" panel

  if (url.substr(0, PREFIX.size()) != PREFIX) return false;
  const size_t hostEnd = url.find('/', PREFIX.size());
  const size_t hostLen = hostEnd == std::string_view::npos ? url.size() - PREFIX.size() : hostEnd - PREFIX.size();
  if (hostLen == 0 || hostLen > MAX_HOST_LENGTH) return false;

  const size_t total = PREFIX.size() + hostLen + SUFFIX.size();
  if (size < total + 1) return false;
  std::memcpy(out, PREFIX.data(), PREFIX.size());
  std::memcpy(out + PREFIX.size(), url.data() + PREFIX.size(), hostLen);
  std::memcpy(out + PREFIX.size() + hostLen, SUFFIX.data(), SUFFIX.size());
  out[total] = '\0';
  return true;
}

}  // namespace ybxaccount
