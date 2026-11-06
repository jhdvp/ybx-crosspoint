#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace fake {
inline bool insecureCalled = false;
inline bool trustedClient = false;
inline unsigned authHeaders = 0;
inline int requests = 0;
}  // namespace fake

namespace freeink {
class SecureHttpClient {
 public:
  void setInsecure() { fake::insecureCalled = true; }
  void setCACert(const char*) { fake::trustedClient = true; }
  bool begin(const std::string& url) {
    url_ = url;
    return fake::trustedClient || url.rfind("http://", 0) == 0;
  }
  int GET() { return 200; }
  void end() {}
  int sendRequest(const char*, const std::string&) { return 200; }
  std::string getString() const { return "{}"; }
  void addHeader(const std::string& key, const std::string&) {
    if (key == "x-auth-user" || key == "x-auth-key" || key == "Authorization") ++fake::authHeaders;
  }

 private:
  std::string url_;
};
}  // namespace freeink
