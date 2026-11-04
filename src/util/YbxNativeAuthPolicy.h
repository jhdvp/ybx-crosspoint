#pragma once
#include <cstdint>
#include <string_view>

// Portable protocol guards for the built-in YBX device-code sign-in. Never
// follow a server-supplied URI or show a QR for an untrusted authority.
namespace ybxnative {
constexpr bool trustedVerifyUrl(std::string_view url) {
  constexpr std::string_view root = "https://my.ybx.app/";
  constexpr std::string_view alternate = "https://ybx.app/";
  if (url.size() > 512 || url.empty()) return false;
  const bool allowed = url.substr(0, root.size()) == root || url.substr(0, alternate.size()) == alternate;
  if (!allowed) return false;
  for (const char ch : url)
    if (ch <= 0x20 || ch == 0x7f) return false;
  return true;
}
constexpr bool validDeviceChallenge(std::string_view userCode, std::string_view deviceCode,
                                    std::string_view verifyUrl) {
  if (userCode.empty() || userCode.size() > 64 || deviceCode.empty() || deviceCode.size() > 1024 ||
      !trustedVerifyUrl(verifyUrl))
    return false;
  for (const char ch : userCode) {
    if (static_cast<unsigned char>(ch) < 0x20) return false;
  }
  return true;
}
constexpr uint32_t pollSeconds(int64_t raw) { return raw < 3 ? 3 : raw > 60 ? 60 : static_cast<uint32_t>(raw); }
constexpr uint32_t expiresSeconds(int64_t raw) {
  return raw < 60 ? 60 : raw > 1800 ? 1800 : static_cast<uint32_t>(raw);
}
constexpr bool pendingGrantError(std::string_view error) {
  return error == "authorization_pending" || error == "slow_down";
}
constexpr bool terminalGrantError(std::string_view error) {
  return error == "expired_token" || error == "access_denied" || error == "invalid_grant" || error == "invalid_client";
}
}  // namespace ybxnative
