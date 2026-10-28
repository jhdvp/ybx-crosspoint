#include "OpdsHttpTransport.h"

#include <Arduino.h>

#include "network/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace fo = freeink::opds;

void OpdsHttpTransport::beginFetch(const uint32_t now, const uint32_t budgetMs) {
  limiter_.begin(now, budgetMs);
  // Make the first abort check poll input immediately, not 50 ms in.
  lastInputPollMs_ = now - INPUT_POLL_INTERVAL_MS;
}

void OpdsHttpTransport::endFetch() { limiter_.end(); }

bool OpdsHttpTransport::abortRequested() {
  if (!limiter_.active()) return false;
  const uint32_t now = millis();
  if (inputPoll_ && now - lastInputPollMs_ >= INPUT_POLL_INTERVAL_MS) {
    lastInputPollMs_ = now;
    if (inputPoll_()) limiter_.cancel();
  }
  return limiter_.abortRequested(now);
}

fo::HttpResult OpdsHttpTransport::get(const std::string& url, const fo::HttpAuth& auth, const char* accept,
                                      const char* acceptLanguage, const bool captureErrorBody,
                                      const fo::OpdsDataSink& onChunk) {
  int status = 0;
  HttpDownloader::FetchOptions options;
  options.username = auth.username;
  options.password = auth.password;
  options.bearer = auth.bearer;
  options.headers = auth.headers;
  if (accept) options.accept = accept;
  if (acceptLanguage) options.acceptLanguage = acceptLanguage;
  options.captureErrorBody = captureErrorBody;
  options.statusOut = &status;
  options.timeoutMs = fetchTimeoutMs(millis());
  options.shouldAbort = [this] { return abortRequested(); };
  const bool ok = HttpDownloader::fetchUrl(url, onChunk, options);
  return fo::HttpResult{ok, status};
}

bool OpdsHttpTransport::postForm(const std::string& url, const std::string& body, std::string& outResponse,
                                 int& outStatus) {
  return HttpDownloader::postForm(url, body, outResponse, &outStatus, fetchTimeoutMs(millis()),
                                  [this] { return abortRequested(); });
}

std::string OpdsHttpTransport::resolveUrl(const std::string& base, const std::string& ref) {
  return UrlUtils::buildUrl(base, ref);
}
