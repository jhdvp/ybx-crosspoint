#include <ArduinoJson.h>
#include <ClippingStore.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <SecureHttpClient.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "BuildScratch.h"
#include "clippings/ClippingSync.h"
#include "network/TlsTransport.h"
#include "util/YbxNoteContextStore.h"

namespace {
constexpr const char* book = "/book.epub";
std::vector<uint8_t> framebuffer(52272);

void reset() {
  CLIPPINGS.unload();
  fake::reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  fake::syncHeap = 100000;
#else
  fake::syncHeap = 68000;
#endif
  fake::syncBufferAllocated = false;
  fake::uploads.clear();
  fake::anchorUploads.clear();
  fake::anchorRequests = 0;
  fake::anchorsSupported = true;
  fake::anchorResponse = "{\"anchor_version\":1,\"cursor\":0,\"more\":false,\"items\":[]}";
  fake::requests = 0;
  fake::failHttpAt = -1;
  fake::incomplete = false;
  fake::insecureCalled = false;
  fake::trustedClient = false;
  fake::authHeaders = 0;
  fake::allowVerifiedTls = true;
  fake::tlsPrepared = false;
  fake::tlsAttempts = 0;
  syncCredentials.baseUrl = "https://sync.test";
  fake::remoteResponse = "{\"cursor\":0,\"more\":false,\"items\":[]}";
  syncCredentials.enabled = true;
}
void released() {
  auto* scratch = buildscratch::claim(32768);
  assert(scratch == framebuffer.data());
  buildscratch::release(scratch);
}
void add(const std::string& text) {
  assert(CLIPPINGS.loadForBook(book, "Book", "Author", "epub"));
  assert(CLIPPINGS.addClipping(0, 0, 0, 1, 0, 0, 1, "Chapter", 0, text, 1, 0, 3) == ClippingStore::AddResult::Added);
  CLIPPINGS.unload();
}
void maximumTextRoundTrip() {
  reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  fake::syncHeap = 100000;
#endif
  const std::string text(CLIPPING_TEXT_MAX, '\t');
  add(text);
  JsonDocument response;
  response["cursor"] = 1;
  response["more"] = false;
  auto remote = response["items"].to<JsonArray>().add<JsonObject>();
  remote["id"] = "remote";
  remote["deleted"] = 0;
  remote["revision"] = 1;
  for (auto key : {"spine", "start_page", "end_page", "pages", "start_word", "end_word", "words", "created_at",
                   "layout_signature"})
    remote[key] = 1;
  std::string escapedText = "\"";
  for (size_t i = 0; i < CLIPPING_TEXT_MAX; ++i) escapedText += "\\u0009";
  escapedText += '"';
  remote["text"] = serialized(escapedText);
  fake::remoteResponse.clear();
  serializeJson(response, fake::remoteResponse);
  assert(fake::remoteResponse.size() > 24 * 1024);
  assert(clippingSync::run(book, "hash"));
  released();
  assert(fake::uploads.size() == 1);
  assert(fake::uploads.front().size() > 8 * 1024);
  JsonDocument upload;
  assert(!deserializeJson(upload, fake::uploads.front()));
  assert(upload["items"][0]["text"].as<std::string>() == text);
  assert(CLIPPINGS.loadForBook(book, "", "", "epub"));
  assert(CLIPPINGS.clippingCount() == 2);
  std::string stored;
  assert(CLIPPINGS.readClippingText(1, stored));
  assert(stored == text);
  CLIPPINGS.unload();
}
constexpr const char* nativeBook = "/apps/notes/note-0123456789abcdef.md";
constexpr const char* nativeText = "A complete unique selection.";

std::string addNative(bool localContext) {
  assert(CLIPPINGS.loadForBook(nativeBook, "Native", "", "epub"));
  assert(CLIPPINGS.addClipping(0, 0, 0, 1, 0, 0, 4, "Note", 0,
                              nativeText, 112233, 0, 27) == ClippingStore::AddResult::Added);
  const Clipping* clip = CLIPPINGS.clippingAt(0);
  assert(clip);
  std::string id = clip->id;
  if (localContext)
    assert(ybxnote::contextstore::save(CLIPPINGS.contextAnchorPath(), id, nativeText,
                                      {"Before ", " after."}));
  CLIPPINGS.unload();
  return id;
}

void compatibleOldServerKeepsPrimaryClippingSync() {
  reset();
  addNative(true);
  fake::anchorsSupported = false;
  assert(clippingSync::run(nativeBook, "filename-md5"));
  assert(fake::uploads.size() == 1);
  assert(fake::anchorUploads.empty());
  assert(fake::anchorRequests == 1);
  assert(fake::authHeaders >= 2);
  released();
}

void sendsLocalContextAfterPrimaryQuoteAndAcceptsIdempotentRetry() {
  reset();
  const std::string id = addNative(true);
  assert(clippingSync::run(nativeBook, "filename-md5"));
  assert(fake::uploads.size() == 1);
  assert(fake::anchorUploads.size() == 1);
  JsonDocument parsed;
  assert(!deserializeJson(parsed, fake::anchorUploads[0]));
  assert(parsed["anchor_version"].as<unsigned>() == 1);
  assert(parsed["id"].as<std::string>() == id);
  assert(parsed["text"].as<std::string>() == nativeText);
  assert(parsed["prefix"].as<std::string>() == "Before ");
  assert(clippingSync::run(nativeBook, "filename-md5"));
  assert(fake::uploads.size() == 1);  // clipping revision is unchanged
  assert(fake::anchorUploads.size() == 2);  // anchor PUT is idempotent
  released();
}

void savesFetchedContextForQuoteAlreadySyncedViaV2() {
  reset();
  const std::string id = addNative(false);
  JsonDocument response;
  response["anchor_version"] = 1;
  response["cursor"] = 1;
  response["more"] = false;
  JsonObject item = response["items"].to<JsonArray>().add<JsonObject>();
  item["id"] = id;
  item["text"] = nativeText;
  item["prefix"] = "Prelude ";
  item["suffix"] = " tail";
  item["revision"] = 1;
  serializeJson(response, fake::anchorResponse);
  assert(clippingSync::run(nativeBook, "filename-md5"));
  assert(fake::anchorUploads.empty());
  assert(CLIPPINGS.loadForBook(nativeBook, "", "", "epub"));
  ybxnote::Context result;
  assert(ybxnote::contextstore::read(CLIPPINGS.contextAnchorPath(), id, nativeText, result) ==
         ybxnote::contextstore::Status::Found);
  assert(result.prefix == "Prelude ");
  CLIPPINGS.unload();
  released();
}

void refusesInvalidAnchorVersionOrNonAdvancingCursor() {
  for (const char* badResponse : {
       "{\"anchor_version\":2,\"cursor\":0,\"more\":false,\"items\":[]}",
       "{\"anchor_version\":1,\"cursor\":0,\"more\":true,\"items\":[]}",
       "{\"anchor_version\":1,\"cursor\":0,\"more\":true,\"items\":["
         "{\"id\":\"0123456789abcdef0123456789abcdef\",\"text\":\"A complete unique selection.\","
         "\"prefix\":\"pre\",\"suffix\":\"after\",\"revision\":1}]}",
       "{\"anchor_version\":1,\"cursor\":1,\"more\":false,\"items\":["
         "{\"id\":\"0123456789abcdef0123456789abcdef\",\"text\":\"A complete unique selection.\","
         "\"prefix\":\"pre\",\"suffix\":\"after\",\"revision\":\"bad\"}]}",
  }) {
    reset();
    addNative(false);
    fake::anchorResponse = badResponse;
    assert(!clippingSync::run(nativeBook, "filename-md5"));
    assert(fake::anchorUploads.empty());
    released();
  }
}

void skipsStaleLocalQuoteContextWithoutBlockingClippingSync() {
  reset();
  const std::string id = addNative(true);
  // Change the existing clipping content with the same ID as an incoming
  // v2 revision would; the captured sidecar is now intentionally stale.
  assert(CLIPPINGS.loadForBook(nativeBook, "Native", "", "epub"));
  Clipping newer = *CLIPPINGS.clippingAt(0);
  newer.syncRevision = 2;
  assert(CLIPPINGS.applyRemote(newer, "A different quote after editing.", false));
  CLIPPINGS.unload();
  assert(clippingSync::run(nativeBook, "filename-md5"));
  assert(fake::anchorUploads.empty());
  assert(CLIPPINGS.loadForBook(nativeBook, "", "", "epub"));
  std::string text;
  assert(CLIPPINGS.readClippingText(0, text) && text == "A different quote after editing.");
  ybxnote::Context previous;
  assert(ybxnote::contextstore::read(CLIPPINGS.contextAnchorPath(), id, text, previous) ==
         ybxnote::contextstore::Status::Stale);
  CLIPPINGS.unload();
  released();
}

void refusesRemoteAnchorForEditedQuoteButKeepsLocalClip() {
  reset();
  const std::string id = addNative(false);
  JsonDocument response;
  response["anchor_version"] = 1;
  response["cursor"] = 1;
  response["more"] = false;
  JsonObject item = response["items"].to<JsonArray>().add<JsonObject>();
  item["id"] = id;
  item["text"] = "Another changed selected quote.";
  item["prefix"] = "Prelude ";
  item["suffix"] = " tail";
  item["revision"] = 1;
  serializeJson(response, fake::anchorResponse);
  assert(!clippingSync::run(nativeBook, "filename-md5"));
  assert(CLIPPINGS.loadForBook(nativeBook, "", "", "epub"));
  std::string current;
  assert(CLIPPINGS.readClippingText(0, current) && current == nativeText);
  CLIPPINGS.unload();
  released();
}

}  // namespace

int main() {
  buildscratch::lend(framebuffer.data(), framebuffer.size());
  maximumTextRoundTrip();
  compatibleOldServerKeepsPrimaryClippingSync();
  sendsLocalContextAfterPrimaryQuoteAndAcceptsIdempotentRetry();
  savesFetchedContextForQuoteAlreadySyncedViaV2();
  refusesRemoteAnchorForEditedQuoteButKeepsLocalClip();
  skipsStaleLocalQuoteContextWithoutBlockingClippingSync();
  refusesInvalidAnchorVersionOrNonAdvancingCursor();
  for (int failure = 0; failure < 3; ++failure) {
    reset();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
    fake::syncHeap = 100000;
#endif
    if (failure == 0) fake::failHttpAt = 0;
    if (failure == 1) fake::incomplete = true;
    if (failure == 2) fake::remoteResponse.assign(32768, 'x');
    assert(!clippingSync::run(book, "hash"));
    released();
  }
  reset();
  fake::failAlloc = 0;
  assert(!clippingSync::run(book, "hash"));
  released();
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
  reset();
  fake::syncHeap = 68000;
  assert(!clippingSync::run(book, "hash"));  // Preserve the S3 heap guard.
#endif
  // Account credentials must never be sent over HTTP or unverified TLS,
  // including on the old anonymous clipping upload path.
  reset();
  syncCredentials.baseUrl = "http://sync.test";
  assert(!clippingSync::run(book, "hash"));
  assert(fake::tlsAttempts == 1);
  assert(fake::requests == 0);
  assert(fake::authHeaders == 0);
  assert(!fake::insecureCalled);
  released();
  reset();
  fake::allowVerifiedTls = false;  // unavailable clock / bad CA
  assert(!clippingSync::run(book, "hash"));
  assert(fake::tlsAttempts == 1);
  assert(fake::requests == 0);
  assert(fake::authHeaders == 0);
  released();
  reset();
  assert(clippingSync::run(book, "hash"));
  assert(fake::tlsPrepared && fake::trustedClient && !fake::insecureCalled);
  assert(fake::authHeaders >= 2);
  released();

  buildscratch::reclaim();
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
  reset();
  assert(!clippingSync::run(book, "hash"));  // Missing loan must fail safely.
  assert(fake::requests == 0);
#endif
  std::puts("Clipping sync tests passed");
}
