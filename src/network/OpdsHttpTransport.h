#pragma once
#include <OpdsTransport.h>

#include <cstdint>
#include <functional>
#include <utility>

#include "network/CatalogFetchLimiter.h"

/**
 * Firmware implementation of the SDK OpdsClient's transport: streaming GET and
 * form POST over HttpDownloader (wolfSSL), and URL joining through UrlUtils.
 *
 * A fetch window (CatalogFetchLimiter) bounds one logical catalog operation —
 * a feed load, a publication detail, a license exchange — opened by the
 * activity with beginFetch(). Its absolute deadline and the user's cancel are
 * handed to every request inside the window. The HTTP client's per-read
 * timeout restarts on every byte, so the abort callback is what actually
 * enforces the deadline; the socket timeout is only clamped to what remains.
 */
class OpdsHttpTransport final : public freeink::opds::OpdsTransport {
 public:
  freeink::opds::HttpResult get(const std::string& url, const freeink::opds::HttpAuth& auth, const char* accept,
                                const char* acceptLanguage, bool captureErrorBody,
                                const freeink::opds::OpdsDataSink& onChunk) override;

  bool postForm(const std::string& url, const std::string& body, std::string& outResponse, int& outStatus) override;

  std::string resolveUrl(const std::string& base, const std::string& ref) override;

  // Opens a new bounded window; `now` is millis(), and any previous window is
  // replaced. Back is polled from the first abort check of the new window.
  void beginFetch(uint32_t now, uint32_t budgetMs = freeink::network::CatalogFetchLimiter::DEFAULT_BUDGET_MS);
  // Closes the window once the logical operation is over, on success and on
  // failure alike. Read stopReason() before calling this: end clears it.
  void endFetch();
  // Why the window stopped: None while running or after a normal finish.
  freeink::network::CatalogFetchLimiter::Stop stopReason() const { return limiter_.stop(); }
  // Socket timeout for the request(s) of the current window, in ms; 0 keeps
  // the HTTP layer default.
  uint32_t fetchTimeoutMs(uint32_t now) const { return limiter_.socketTimeoutMs(now); }
  // True when the window's deadline passed or the user cancelled. The HTTP
  // layer polls it as its abort callback while connecting and reading.
  bool abortRequested();
  // Called from abortRequested() (throttled to INPUT_POLL_INTERVAL_MS): return
  // true to cancel the window. Lets Back reach a fetch that owns the loop.
  void setInputPoll(std::function<bool()> poll) { inputPoll_ = std::move(poll); }

 private:
  static constexpr uint32_t INPUT_POLL_INTERVAL_MS = 50;

  freeink::network::CatalogFetchLimiter limiter_;
  std::function<bool()> inputPoll_;
  uint32_t lastInputPollMs_ = 0;
};
