#pragma once
#include <string>

// Account identity belongs to the YBX Reader firmware, not to an optional
// catalog plugin. Legacy plugin tokens remain readable during migration.
namespace ybxnative {
inline constexpr const char* kAccountFile = "/.crosspoint/ybx-account.json";
inline constexpr const char* kCacheRoot = "/.crosspoint/ybx";
inline constexpr const char* kDeviceUrl = "https://my.ybx.app/api/v1/crosspoint/auth/device";
inline constexpr const char* kTokenUrl = "https://my.ybx.app/api/v1/crosspoint/auth/token";
inline constexpr const char* kListsUrl = "https://my.ybx.app/api/v1/crosspoint/catalog/lists";
inline constexpr const char* kSummaryUrl = "https://my.ybx.app/api/v1/crosspoint/reading/summary?limit=1";
inline constexpr const char* kAwardsUrl = "https://my.ybx.app/api/v1/crosspoint/achievements?limit=50";

// Never print or log reader tokens. Callers own and discard the returned key.
bool loadToken(std::string& out);
// Atomic SD write. Only after a successful device-code grant; mirrors existing
// optional plugin token for backwards compatibility, never requires a plugin.
bool saveToken(const std::string& token);
}  // namespace ybxnative
