#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "BuildScratch.h"

namespace fake {
using Headers = std::vector<std::pair<std::string, std::string>>;
inline std::vector<Headers> headers;  // One entry per begin(), in request order.
inline std::vector<std::string> uploads;
inline std::vector<std::string> anchorUploads;
inline std::string remoteResponse;
inline std::string anchorResponse = "{\"anchor_version\":1,\"cursor\":0,\"more\":false,\"items\":[]}";
inline bool anchorsSupported = true;
inline int anchorRequests = 0;
inline int requests = 0;
inline int failHttpAt = -1;
inline bool incomplete = false;
inline bool insecureCalled = false;
inline bool trustedClient = false;
}  // namespace fake
namespace freeink {
class SecureHttpClient {
 public:
  void setInsecure() { fake::insecureCalled = true; }
  void setCACert(const char*) { fake::trustedClient = true; }
  bool begin(const std::string& url) {
    url_ = url;
    fake::headers.emplace_back();
    return fake::trustedClient || url.rfind("http://", 0) == 0;
  }
  void end() {}
  int sendRequest(const char*, const std::string&) { return 200; }
  std::string getString() const { return "{}"; }
  void addHeader(const std::string& name, const std::string& value) { fake::headers.back().emplace_back(name, value); }
  template <typename Sink>
  int sendRequest(const char* method, const uint8_t* data, size_t length, Sink sink) {
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
    // The transfer must hold the exclusive scratch claim until its response is parsed.
    assert(buildscratch::claim(32768) == nullptr);
    assert(!fake::syncBufferAllocated);
#endif
    if (fake::requests++ == fake::failHttpAt) return 500;
    const bool upload = std::string(method) == "PUT";
    const bool anchor = url_.find("/anchors") != std::string::npos;
    if (anchor) ++fake::anchorRequests;
    complete_ = !fake::incomplete;
    if (anchor && !fake::anchorsSupported) return 404;
    if (upload && anchor)
      fake::anchorUploads.emplace_back(reinterpret_cast<const char*>(data), length);
    else if (upload)
      fake::uploads.emplace_back(reinterpret_cast<const char*>(data), length);
    const std::string response = anchor   ? (upload ? "{\"accepted\":1,\"revision\":1}" : fake::anchorResponse)
                                 : upload ? "{\"accepted\":1}"
                                 : url_.find("?cursor=0") != std::string::npos ? "{\"sync_version\":2}"
                                                                               : fake::remoteResponse;
    for (size_t offset = 0; offset < response.size(); offset += 97) {
      const size_t chunk = std::min<size_t>(97, response.size() - offset);
      if (!sink(reinterpret_cast<const uint8_t*>(response.data() + offset), chunk)) {
        complete_ = false;
        break;
      }
    }
    return 200;
  }
  bool responseComplete() const { return complete_; }

 private:
  std::string url_;
  bool complete_ = false;
};
}  // namespace freeink
