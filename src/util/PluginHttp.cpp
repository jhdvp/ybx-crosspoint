#include "PluginHttp.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "network/ProtectedPaths.h"
#include "network/TlsTransport.h"
#include "network/WifiPowerSaveGuard.h"
#include "util/PluginShelf.h"
#include "util/YbxServerMirror.h"

namespace {
constexpr size_t MAX_TOKEN_FILE_SIZE = 2 * 1024;
constexpr size_t MAX_DEVICE_MANIFEST_SIZE = 8 * 1024;

// Millis() units at which one mirror sequence must stop. Everything left of it
// is the budget; handing each candidate a fresh timeoutMs would let a dead
// service block the caller for count times the timeout, which on the main task
// is a frozen reader.
uint32_t mirrorBudget(const uint32_t deadline) {
  const int32_t left = static_cast<int32_t>(deadline - millis());
  return left > 0 ? static_cast<uint32_t>(left) : 0;
}

// Resolves the caller's reused TLS session (or `tmp`) into a configured,
// connected client, or nullptr when the caller aborted, begin() failed or the
// connection failed. `connectMs` (always non-zero, from mirrorSequence) bounds
// DNS, TCP and the handshake for this candidate; the response then runs on what
// the sequence deadline still allows.
//
// `hostReached` reports the one failure that is not about reachability: the
// transport connected, then no budget was left for a response. Such a host is
// neither remembered nor throttled (mirrorSequence records nothing), and the
// connection is closed here so a reused session does not keep a socket and its
// TLS working set until the next request.
freeink::SecureHttpClient* openClient(freeink::SecureHttpClient* session, freeink::SecureHttpClient& tmp,
                                      const std::string& url, const pluginhttp::Headers& headers,
                                      const std::function<bool()>& shouldAbort, const uint32_t connectMs,
                                      const uint32_t deadlineMs, bool& hostReached) {
  if (shouldAbort && shouldAbort()) return nullptr;
  freeink::SecureHttpClient& http = session ? *session : tmp;
  http.setUserAgent("YBX");
  http.setTimeout(connectMs);
  if (!http.begin(pluginhttp::escapeUrl(url))) return nullptr;
  // Plugin endpoints carry the reader's YBX token: https only, with the peer
  // chain and host name verified against the firmware CA bundle, and only
  // once the clock can validate certificate dates.
  if (!tlspolicy::prepareHttpsClient(http, url, connectMs, shouldAbort)) return nullptr;
  // Handshake first: the client's copy of a large header (a session cookie can
  // be several KB) would otherwise take heap the handshake needs.
  if (!http.connect(shouldAbort)) {
    if (!http.aborted()) {
      tlspolicy::recordFailure(tlspolicy::failureFromConnect(http.connectFailure()));
    }
    return nullptr;
  }
  // Connected: a bounded probe must not shorten the real transfer, so the
  // response gets the rest of the sequence budget.
  const uint32_t responseMs = mirrorBudget(deadlineMs);
  if (responseMs == 0) {
    hostReached = true;
    http.end();
    return nullptr;
  }
  http.setTimeout(responseMs);
  for (const auto& h : headers) http.addHeader(h.first, h.second);
  return &http;
}

// A chunked body's Content-Length, if any, is not its size.
bool hasKnownLength(const freeink::SecureHttpClient& http) {
  return http.hasContentLength() && http.getHeader("transfer-encoding").empty();
}

// What one attempt proved about the host it was given. Only a reply (or a body
// error) and an unreachable transport are evidence the mirror preference can
// act on: a host whose handshake spent the whole remaining budget was reached,
// yet said nothing, so the sequence records neither.
enum class Attempt : uint8_t { Answered, Unreachable, ReachedWithoutAnswer };

// One mirror sequence: probe the plan's candidates in order against a single
// absolute deadline, and record what the probes proved. `once` performs one
// attempt for the candidate it is given.
//
// Each candidate gets its own bounded slice of the deadline for DNS, TCP and
// the handshake (ybxmirror::connectSliceMs); a blackhole host then costs one
// slice instead of the whole budget and the mirrors are still reached. The
// response phase is not sliced, because only a failed connect says anything
// about the host: an HTTP reply (including 4xx and 5xx) or a body error stays
// on the host that produced it.
//
// Preference rules: a host that replied is remembered; every candidate probed
// and not reached is what afterAllFailed throttles; a reached-but-unanswered
// host and a user abort record nothing.
template <typename OnceFn>
int mirrorSequence(const std::string& url, const std::function<bool()>& shouldAbort, const uint32_t timeoutMs,
                   const OnceFn& once) {
  ybxmirror::Plan plan(url);
  const uint32_t deadline = millis() + timeoutMs;
  int status = -1;
  uint8_t probed = 0;
  bool aborted = false;
  for (uint8_t i = 0; i < plan.count(); i++) {
    const uint32_t budget = mirrorBudget(deadline);
    const uint32_t connectMs = ybxmirror::connectSliceMs(budget, plan.count() - i);
    // No slice means no time for even one probe. Stopping here keeps `probed`
    // honest: a host we never reached is not evidence that it is dead.
    if (connectMs == 0) break;
    Attempt attempt = Attempt::Answered;
    status = once(plan.urlAt(i), connectMs, deadline, attempt);
    if (attempt == Attempt::Answered) {
      plan.answered(i);
      return status;
    }
    if (attempt == Attempt::ReachedWithoutAnswer) return status;
    if (shouldAbort && shouldAbort()) {
      aborted = true;
      break;
    }
    probed = i + 1;
  }
  // Throttle what was actually proved unreachable. Probing stops early when the
  // budget is spent, and forgetting that would make every later request pay for
  // the dead host all over again.
  if (ybxmirror::recordsAllFailed(probed, aborted)) plan.allFailed();
  return status;
}
}  // namespace

namespace pluginhttp {

// True when a dotted-path segment addresses an array index.
static bool segIsIndex(const std::string& seg) { return !seg.empty() && isdigit(static_cast<unsigned char>(seg[0])); }

void splitPath(const std::string& dotted, std::vector<std::string>& out) {
  out.clear();
  out.reserve(static_cast<size_t>(std::count(dotted.begin(), dotted.end(), '.')) + 1);
  size_t start = 0;
  while (start <= dotted.size()) {
    const size_t dot = dotted.find('.', start);
    if (dot == std::string::npos) {
      if (start < dotted.size()) out.push_back(dotted.substr(start));
      break;
    }
    if (dot > start) out.push_back(dotted.substr(start, dot - start));
    start = dot + 1;
  }
}

JsonVariantConst resolvePath(JsonVariantConst node, const std::string& dotted) {
  if (dotted.empty()) return node;
  std::vector<std::string> segs;
  splitPath(dotted, segs);
  for (const auto& seg : segs) {
    if (node.isNull()) break;
    if (segIsIndex(seg)) {
      node = node[atoi(seg.c_str())];
    } else {
      node = node[seg.c_str()];
    }
  }
  return node;
}

std::string variantToString(JsonVariantConst v) {
  if (v.isNull()) return "";
  if (v.is<const char*>()) return v.as<const char*>();
  char buf[24];
  if (v.is<long long>() || v.is<int>()) {
    snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
    return buf;
  }
  return "";
}

void substituteAll(std::string& s, const char* key, const std::string& value) {
  const size_t keyLen = strlen(key);
  size_t pos = 0;
  while ((pos = s.find(key, pos)) != std::string::npos) {
    s.replace(pos, keyLen, value);
    pos += value.size();
  }
}

void readRequest(JsonVariantConst node, const char* defaultMethod, RequestSpec& out) {
  out.url = node["url"] | "";
  out.method = node["method"] | defaultMethod;
  out.body = node["body"] | "";
  readHeaders(node["headers"], out.headers);
}

void readHeaders(JsonVariantConst node, Headers& out) {
  out.clear();
  const JsonObjectConst headers = node.as<JsonObjectConst>();
  out.reserve(headers.size());
  for (JsonPairConst kv : headers) {
    out.emplace_back(kv.key().c_str(), kv.value().as<const char*>() ? kv.value().as<const char*>() : "");
  }
}

std::string urlEncodeQuery(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string escapeUrl(const std::string& url) {
  const auto kept = [](const unsigned char c) { return c > 0x20 && c < 0x7F; };
  const size_t escaped =
      std::count_if(url.begin(), url.end(), [&](const char c) { return !kept(static_cast<unsigned char>(c)); });
  std::string out;
  out.reserve(url.size() + 2 * escaped);
  for (const unsigned char c : url) {
    if (kept(c)) {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string inPluginDir(const std::string& pluginDir, const char* path) {
  if (!path || !path[0] || path[0] == '/') return path ? path : "";
  return pluginDir + "/" + path;
}

// A manifest may not point the firmware at a credential store, and not at a
// file in the reserved marker namespace either: token and config writes are
// plugin-supplied paths too.
static bool allowedFile(const std::string& file) {
  if (pluginshelf::isWriteTargetAllowed(file)) return true;
  LOG_ERR("PHTP", "Manifest file not allowed: %s", file.c_str());
  return false;
}

bool loadTokenFromFile(const std::string& file, const std::string& path, std::string& out) {
  out.clear();
  if (file.empty()) return true;  // token-less plugin
  if (!allowedFile(file)) return false;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;
  out = variantToString(resolvePath(doc.as<JsonVariantConst>(), path));
  return !out.empty();
}

bool saveTokenToFile(const std::string& file, const std::string& path, const std::string& value) {
  if (file.empty()) return false;
  if (!allowedFile(file)) return false;
  JsonDocument doc;
  // Build the nesting the read path expects (numeric segments unsupported).
  std::vector<std::string> segs;
  splitPath(path, segs);
  if (segs.empty()) return false;
  JsonVariant node = doc.to<JsonObject>();
  for (size_t i = 0; i + 1 < segs.size(); i++) node = node[segs[i]].to<JsonObject>();
  node[segs.back()] = value;
  String out;
  serializeJson(doc, out);
  if (doc.overflowed() || out.length() > MAX_TOKEN_FILE_SIZE || out.length() != measureJson(doc)) {
    LOG_ERR("PHTP", "Invalid or oversized token JSON");
    return false;
  }
  if (!Storage.writeFile(file.c_str(), out)) {
    LOG_ERR("PHTP", "Failed to save token file");
    return false;
  }
  return true;
}

void loadConfigFile(const std::string& file, Headers& out) {
  out.clear();
  if (file.empty()) return;
  if (!allowedFile(file)) return;
  std::string raw;
  if (!Storage.readFileToString("PHTP", file, MAX_TOKEN_FILE_SIZE, raw)) return;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
  const JsonObjectConst fields = doc.as<JsonObjectConst>();
  out.reserve(fields.size());
  for (JsonPairConst kv : fields) {
    out.emplace_back(kv.key().c_str(), variantToString(kv.value()));
  }
}

std::string pickDownloadDir(const Headers& config, const std::string& manifestDestDir) {
  for (const auto& kv : config) {
    if (kv.first != "dest_dir" || kv.second.empty()) continue;
    std::string dir = kv.second.front() == '/' ? kv.second : "/" + kv.second;
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if (dir == "/") return "";  // SD root
    if (protectedpaths::isPluginPath(dir)) return dir;
    LOG_ERR("PHTP", "config dest_dir rejected: %s", kv.second.c_str());
  }
  // One canonical spelling for the manifest value too: the home "On Reader"
  // folder and the locked file browser compare it as a root, and a manifest
  // writing "dest_dir": "/books/ybx/" would otherwise never equal the path the
  // browser derives by dropping one component.
  return FsHelpers::canonicalFolderPath(manifestDestDir);
}

std::string catalogDownloadDir(const std::string& pluginDir) {
  if (pluginDir.empty()) return "";
  std::string raw;
  if (!Storage.readFileToString("PHTP", pluginDir + "/device.json", MAX_DEVICE_MANIFEST_SIZE, raw)) return "";
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return "";
  std::string manifestDir = inPluginDir(pluginDir, doc["download"]["dest_dir"] | "");
  if (!manifestDir.empty() && !protectedpaths::isPluginPath(manifestDir)) {
    LOG_ERR("PHTP", "device.json dest_dir outside plugin space: %s", manifestDir.c_str());
    manifestDir.clear();
  }
  Headers config;
  loadConfigFile(inPluginDir(pluginDir, doc["config"]["file"] | ""), config);
  return pickDownloadDir(config, manifestDir);
}

static int requestOnce(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                       const std::string& body, const Headers& headers, String& out, const size_t maxResponse,
                       Headers* responseHeaders, const std::function<bool()>& shouldAbort, const uint32_t connectMs,
                       const uint32_t deadlineMs, Attempt& attempt) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  bool hostReached = false;
  freeink::SecureHttpClient* httpPtr =
      openClient(session, tmp, url, headers, shouldAbort, connectMs, deadlineMs, hostReached);
  if (!httpPtr) {
    attempt = hostReached ? Attempt::ReachedWithoutAnswer : Attempt::Unreachable;
    return -1;
  }
  freeink::SecureHttpClient& http = *httpPtr;

  out.remove(0);
  size_t reserved = 0;
  bool overflow = false;
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if (len > maxResponse - out.length()) {
          overflow = true;
          return false;
        }
        const size_t needed = out.length() + len;
        if (needed > reserved) {
          // Reserve the whole body once when its length is known; otherwise grow
          // geometrically instead of reallocating for every network chunk.
          const bool knownLength = hasKnownLength(http);
          size_t want = knownLength ? std::max(needed, http.getContentLength())
                                    : std::min(maxResponse, std::max(needed, reserved * 2));
          if (want > maxResponse) {
            LOG_ERR("PHTP", "Response of %u bytes is over the %u byte cap", (unsigned)want, (unsigned)maxResponse);
            overflow = true;
            return false;
          }
          reserved = want;
        }
        if (!out.reserve(reserved) || !out.concat(reinterpret_cast<const char*>(data), len)) {
          overflow = true;
          return false;
        }
        return true;
      },
      shouldAbort);
  if (responseHeaders) *responseHeaders = http.getHeaders();
  // No HTTP answer at all is a connection failure; an answer whose body was
  // cut short or over the cap is not, and must not move the request elsewhere.
  // A lost answer mid-request is NOT a connection failure: the request was
  // already sent, so retrying it elsewhere could replay a POST or re-send a
  // streamed response's headers. Only a failed connect earns another host.
  // Error statuses still return their body: OAuth device-code polling carries
  // its state ("authorization_pending") in 4xx response bodies.
  if (overflow || status < 0 || !http.responseComplete()) {
    LOG_ERR("PHTP", "API request failed: status=%d overflow=%d complete=%d %s", status, overflow,
            http.responseComplete(), url.c_str());
    return -1;
  }
  return status;
}

int request(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
            const std::string& body, const Headers& headers, String& out, const size_t maxResponse,
            Headers* responseHeaders, const std::function<bool()>& shouldAbort, const uint32_t timeoutMs) {
  // The trust diagnosis is cleared once per request, not per candidate: a
  // certificate problem on the main host must still explain the failure after
  // the mirrors were only reached as timeouts.
  tlspolicy::clearFailure();
  return mirrorSequence(
      url, shouldAbort, timeoutMs,
      [&](const std::string& candidate, const uint32_t connectMs, const uint32_t deadline, Attempt& attempt) {
        return requestOnce(session, candidate, method, body, headers, out, maxResponse, responseHeaders, shouldAbort,
                           connectMs, deadline, attempt);
      });
}

static int requestStreamOnce(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                             const std::string& body, const Headers& headers, const StreamStart& onStart,
                             const StreamData& onData, bool& complete, const std::function<bool()>& shouldAbort,
                             const uint32_t connectMs, const uint32_t deadlineMs, Attempt& attempt) {
  complete = false;
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  bool hostReached = false;
  freeink::SecureHttpClient* httpPtr =
      openClient(session, tmp, url, headers, shouldAbort, connectMs, deadlineMs, hostReached);
  if (!httpPtr) {
    attempt = hostReached ? Attempt::ReachedWithoutAnswer : Attempt::Unreachable;
    return -1;
  }
  freeink::SecureHttpClient& http = *httpPtr;

  bool started = false;
  bool stopped = false;
  const auto start = [&] {
    const long length =
        hasKnownLength(http) ? static_cast<long>(std::min<size_t>(http.getContentLength(), LONG_MAX)) : -1;
    started = onStart(http.getStatus(), http.getHeaders(), length);
    return started;
  };
  const int status = http.sendRequest(
      method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&](const uint8_t* data, size_t len) {
        if ((!started && !start()) || !onData(data, len)) {
          stopped = true;
          return false;
        }
        return true;
      },
      shouldAbort);
  complete = status >= 0 && !stopped && http.responseComplete();
  // An empty body never calls onData, so start here.
  if (complete && !started && !start()) complete = false;
  // A lost answer mid-request is NOT a connection failure: the request was
  // already sent, so retrying it elsewhere could replay a POST or re-send a
  // streamed response's headers. Only a failed connect earns another host.
  if (!complete) {
    LOG_ERR("PHTP", "Streamed request failed: status=%d started=%d stopped=%d %s", status, started, stopped,
            url.c_str());
  }
  return started ? status : -1;
}

int requestStream(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const StreamStart& onStart, const StreamData& onData,
                  bool& complete, const std::function<bool()>& shouldAbort, const uint32_t timeoutMs) {
  tlspolicy::clearFailure();
  return mirrorSequence(
      url, shouldAbort, timeoutMs,
      [&](const std::string& candidate, const uint32_t connectMs, const uint32_t deadline, Attempt& attempt) {
        return requestStreamOnce(session, candidate, method, body, headers, onStart, onData, complete, shouldAbort,
                                 connectMs, deadline, attempt);
      });
}

static int requestToFileOnce(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                             const std::string& body, const Headers& headers, const char* destPath,
                             const size_t maxResponse, const std::function<bool()>& shouldAbort,
                             const uint32_t connectMs, const uint32_t deadlineMs, Attempt& attempt) {
  WifiPowerSaveGuard psGuard;
  freeink::SecureHttpClient tmp;
  bool hostReached = false;
  freeink::SecureHttpClient* httpPtr =
      openClient(session, tmp, url, headers, shouldAbort, connectMs, deadlineMs, hostReached);
  if (!httpPtr) {
    attempt = hostReached ? Attempt::ReachedWithoutAnswer : Attempt::Unreachable;
    return -1;
  }
  freeink::SecureHttpClient& http = *httpPtr;

  int status = -1;
  bool writeOk = true;
  size_t written = 0;
  {
    HalFile file;
    // A local SD failure is not a connection failure: another mirror cannot fix it.
    if (!Storage.openFileForWrite("PHTP", destPath, file)) return -1;
    status = http.sendRequest(
        method.c_str(), reinterpret_cast<const uint8_t*>(body.data()), body.size(),
        [&](const uint8_t* data, size_t len) {
          if (written + len > maxResponse) {
            writeOk = false;
            return false;
          }
          if (file.write(data, len) != len) {
            writeOk = false;
            return false;
          }
          written += len;
          return true;
        },
        shouldAbort);
    file.flush();
    if (!file.close()) writeOk = false;
    // Close before checking/removing the temp file or committing the download.
  }
  // A lost answer mid-request is NOT a connection failure: the request was
  // already sent, so retrying it elsewhere could replay a POST or re-send a
  // streamed response's headers. Only a failed connect earns another host.
  if (!writeOk || status < 0 || !http.responseComplete()) {
    LOG_ERR("PHTP", "API request (to file) failed: status=%d writeOk=%d complete=%d %s", status, writeOk,
            http.responseComplete(), url.c_str());
    Storage.remove(destPath);
    return -1;
  }
  return status;
}

int requestToFile(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                  const std::string& body, const Headers& headers, const char* destPath, const size_t maxResponse,
                  const std::function<bool()>& shouldAbort, const uint32_t timeoutMs) {
  tlspolicy::clearFailure();
  return mirrorSequence(
      url, shouldAbort, timeoutMs,
      [&](const std::string& candidate, const uint32_t connectMs, const uint32_t deadline, Attempt& attempt) {
        return requestToFileOnce(session, candidate, method, body, headers, destPath, maxResponse, shouldAbort,
                                 connectMs, deadline, attempt);
      });
}

bool mintPasswordToken(freeink::SecureHttpClient* session, const std::string& url, const std::string& method,
                       const std::string& body, const Headers& headers, const std::string& tokenPath,
                       std::string& outToken, const std::function<bool()>& shouldAbort, const uint32_t timeoutMs) {
  String response;
  constexpr size_t MAX_AUTH_RESPONSE = 8 * 1024;
  const int status =
      request(session, url, method, body, headers, response, MAX_AUTH_RESPONSE, nullptr, shouldAbort, timeoutMs);
  if (status < 200 || status >= 300) return false;
  JsonDocument doc;
  if (deserializeJson(doc, response) != DeserializationError::Ok) return false;
  outToken = variantToString(resolvePath(doc.as<JsonVariantConst>(), tokenPath));
  return !outToken.empty();
}

}  // namespace pluginhttp
