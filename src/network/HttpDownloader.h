#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * HTTP client utility for fetching content and downloading files. https goes
 * through the wolfSSL transport with the firmware CA bundle (peer chain and
 * host name verified); plain http is used only for explicitly configured
 * local servers, never as a fallback or redirect target.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    UNAUTHORIZED,  // 401/403: callers holding a refreshable credential can retry
    TLS_ERROR,     // certificate/clock/host-name verification or redirect policy
  };

  // Pre-flight floor for starting a TLS transfer. Below this the session or
  // its ~17KB record buffer fails mid-stream (wolfSSL MEMORY_E) -- or an
  // interior allocation abort()s the device. Callers should check before
  // downloadToFile() and fail into their error UI instead.
  static constexpr uint32_t MIN_TLS_FREE_HEAP = 40000;
  static constexpr uint32_t MIN_TLS_MAX_ALLOC = 20000;

  using Header = std::pair<std::string, std::string>;

  /**
   * HTTP status of the last request, or 0 when it never got a response (DNS,
   * connect, TLS, timeout). fetchUrl() returns only success or failure; this
   * lets a caller tell a dead server from a catalog that wants a password.
   * Not thread-safe by design: one fetch runs at a time.
   */
  static int lastStatus();

  /**
   * True when the last answer came from a different origin (scheme, host or
   * port) than the URL asked for, after a redirect. Credentials are withheld
   * from such a hop, so a 401 there is not a wrong password.
   */
  static bool lastAnswerRedirected();

  /**
   * Fetch text content from a URL with optional credentials. `headers` are
   * added to the request, as for downloadToFile().
   */
  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", const std::vector<Header>& headers = {});

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", const std::vector<Header>& headers = {});

  /**
   * Authentication and response inspection options for fetchUrl/postForm.
   * `bearer`, when set, sends "Authorization: Bearer <token>" and overrides
   * Basic credentials. `statusOut` receives the final HTTP status (0 when the
   * request never reached a response). `captureErrorBody` also streams a 401
   * response body to the callback — OPDS authentication documents are served
   * as 401 bodies.
   */
  struct FetchOptions {
    std::string username;
    std::string password;
    std::string bearer;
    // Accept header for content negotiation (e.g. preferring OPDS 2.0 JSON
    // from servers that also speak Atom); empty sends none.
    std::string accept;
    // Accept-Language header, so servers that localize the catalog return it
    // in the reader's UI language; empty sends none.
    std::string acceptLanguage;
    // Extra headers (per-server custom headers), sent to the starting origin only.
    std::vector<Header> headers;
    int* statusOut = nullptr;
    bool captureErrorBody = false;
  };

  static bool fetchUrl(const std::string& url, const DataCallback& onData, const FetchOptions& options);

  /**
   * POST an application/x-www-form-urlencoded body (e.g. an OAuth token
   * request) and collect the response body (any status) into outResponse,
   * capped at a few KB. Returns true when the response status is 2xx.
   */
  static bool postForm(const std::string& url, const std::string& formBody, std::string& outResponse,
                       int* statusOut = nullptr);

  /**
   * Download a file to the SD card with optional credentials. `headers` are
   * added to the request (e.g. a Bearer Authorization), alongside any Basic
   * auth derived from username/password.
   *
   * https downloads verify the peer chain and host name against the firmware
   * CA bundle and are refused until the clock can validate certificate dates;
   * a redirect that steps down to plain http is refused, never followed.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, const bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      const std::vector<Header>& headers = {});

  /**
   * POST a body (e.g. an LCP license to the fulfillment service) and stream
   * the response to the SD card, staged through "<dest>.part" like
   * downloadToFile. Single attempt, no redirects or Range resume: the
   * endpoint is ours and answers directly.
   */
  static DownloadError postToFile(const std::string& url, const std::string& body, const std::string& contentType,
                                  const std::string& destPath, ProgressCallback progress = nullptr,
                                  const bool* cancelFlag = nullptr);
};
