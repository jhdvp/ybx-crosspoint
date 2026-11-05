#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Pure logic behind the native YBX account screen: which connected sources a
// /catalog/lists response describes, what an HTTP status means for the stored
// reader token, and bounded request timing. Header-only and free of
// Arduino dependencies so host tests can cover it.
namespace ybxaccount {

enum class SourceKind : uint8_t { Yandex = 0, Wb, Litres };
constexpr size_t SOURCE_COUNT = 3;

// A catalog list URL names the provider that published it. The aggregated
// endpoint returns one entry per shelf/feed, so the response establishes catalog availability, not provider login
// state.
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

  bool connected(SourceKind kind) const { return (connectedMask & (1u << static_cast<uint8_t>(kind))) != 0; }

  uint16_t shelfCount(SourceKind kind) const { return sources[static_cast<size_t>(kind)].lists; }

  int connectedCount() const {
    int count = 0;
    for (size_t i = 0; i < SOURCE_COUNT; ++i)
      if (connectedMask & (1u << i)) ++count;
    return count;
  }
};

// uint32_t arithmetic matches millis() on both MCU and host tests.
inline bool deadlineReached(const unsigned long nowMs, const unsigned long deadlineMs) {
  return static_cast<int32_t>(static_cast<uint32_t>(nowMs - deadlineMs)) >= 0;
}

}  // namespace ybxaccount
