#include "ClippingSync.h"

#include <ArduinoJson.h>
#include <BuildScratch.h>
#include <HalMemory.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <Utf8.h>

#include <cstring>

#include "ClippingStore.h"
#include "network/TlsTransport.h"
#include "util/YbxDocumentIdentity.h"
#include "util/YbxKosyncDecision.h"
#include "util/YbxNoteContextStore.h"

namespace {
// One item per response: 4 KiB text may occupy 24 KiB when JSON-escaped.
// Reuse one buffer for requests and responses; C3 borrows it from the framebuffer.
constexpr size_t BUFFER_SIZE = 32 * 1024;

bool request(freeink::SecureHttpClient& http, const std::string& url, char* buffer, JsonDocument& doc,
             const bool upload = false, int* returnedStatus = nullptr) {
  size_t payloadSize = 0;
  if (upload) {
    if (doc.overflowed() || measureJson(doc) >= BUFFER_SIZE) {
      LOG_ERR("CLIP", "Clipping request exceeds buffer");
      return false;
    }
    payloadSize = serializeJson(doc, buffer, BUFFER_SIZE);
  }
  doc.clear();
  if (!http.begin(url)) {
    LOG_ERR("CLIP", "Invalid sync URL");
    return false;
  }
  for (const auto& header : KOREADER_STORE.getCustomHeaders()) {
    if (!header.name.empty()) http.addHeader(header.name, header.value);
  }
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  // QR-paired custom firmware auto-configures cp_ on the verified HTTPS YBX
  // KOSync endpoint. That paired key has an explicit clippings scope on YBX,
  // while x-auth-key alone is not accepted on the plugin Clippings v2 alias.
  // Never forward the broad key to another server, an http:// URL, or a
  // homograph/prefix path: those continue using their own account KOSync.
  const std::string& base = KOREADER_STORE.getBaseUrl();
  const std::string& username = KOREADER_STORE.getUsername();
  const std::string& password = KOREADER_STORE.getPassword();
  if (ybxkosync::usePairedKeyForClippings(base, username, password)) {
    http.addHeader("Authorization", "Bearer " + password);
  }
  http.addHeader("Content-Type", "application/json");
  size_t received = 0;
  const int status = http.sendRequest(upload ? "PUT" : "GET", reinterpret_cast<const uint8_t*>(buffer), payloadSize,
                                      [&](const uint8_t* data, const size_t length) {
                                        if (length >= BUFFER_SIZE - received) return false;
                                        memcpy(buffer + received, data, length);
                                        received += length;
                                        return true;
                                      });
  if (returnedStatus) *returnedStatus = status;
  if (status != 200 || !http.responseComplete()) {
    LOG_ERR("CLIP", "Clipping sync HTTP %d (complete=%d)", status, http.responseComplete());
    return false;
  }
  buffer[received] = '\0';
  // Mutable input lets ArduinoJson reference the response buffer without duplicating text.
  const auto error = deserializeJson(doc, buffer, received, DeserializationOption::NestingLimit(4));
  if (error) {
    LOG_ERR("CLIP", "Invalid clipping response: %s", error.c_str());
    return false;
  }
  if (upload && doc["accepted"].as<int>() != 1) {
    LOG_ERR("CLIP", "Server did not acknowledge clipping");
    return false;
  }
  return true;
}

void encode(JsonObject item, const Clipping& clipping, const std::string& text) {
  item["id"] = clipping.id;
  item["spine"] = clipping.spineIndex;
  item["start_page"] = clipping.startPage;
  item["end_page"] = clipping.endPage;
  item["pages"] = clipping.pageCount;
  item["start_word"] = clipping.startWordIndex;
  item["end_word"] = clipping.endWordIndex;
  item["words"] = clipping.wordCount;
  if (clipping.paragraphIndex != UINT16_MAX) item["para"] = clipping.paragraphIndex;
  item["chapter"] = clipping.chapterTitle;
  item["text"] = text.c_str();
  item["created_at"] = clipping.timestamp;
  item["layout_signature"] = clipping.layoutSignature;
  if (clipping.startOffset != UINT32_MAX && clipping.endOffset != UINT32_MAX) {
    item["start_offset"] = clipping.startOffset;
    item["end_offset"] = clipping.endOffset;
  }
}

bool decode(JsonObjectConst item, Clipping& clipping, std::string& text, bool& deleted) {
  const JsonString id = item["id"].as<JsonString>();
  if (id.isNull() || id.size() == 0 || id.size() >= sizeof(clipping.id)) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    if (id.c_str()[i] < '!' || id.c_str()[i] > '~') return false;
  }
  memcpy(clipping.id, id.c_str(), id.size());
  clipping.id[id.size()] = '\0';
  if (!item["deleted"].is<unsigned>() || item["deleted"].as<unsigned>() > 1) return false;
  deleted = item["deleted"].as<unsigned>() == 1;
  if (deleted) return true;
  if (!item["revision"].is<uint64_t>() || !item["spine"].is<uint16_t>() || !item["start_page"].is<uint16_t>() ||
      !item["end_page"].is<uint16_t>() || !item["pages"].is<uint16_t>() || !item["start_word"].is<uint16_t>() ||
      !item["end_word"].is<uint16_t>() || !item["words"].is<uint16_t>() || !item["created_at"].is<uint32_t>() ||
      !item["layout_signature"].is<uint32_t>())
    return false;
  if (!item["para"].isNull() && !item["para"].is<uint16_t>()) return false;
  const JsonString quote = item["text"].as<JsonString>();
  if (quote.isNull() || quote.size() == 0 || quote.size() > CLIPPING_TEXT_MAX || strlen(quote.c_str()) != quote.size())
    return false;
  clipping.spineIndex = item["spine"];
  clipping.startPage = item["start_page"];
  clipping.endPage = item["end_page"];
  clipping.pageCount = item["pages"];
  clipping.startWordIndex = item["start_word"];
  clipping.endWordIndex = item["end_word"];
  clipping.wordCount = item["words"];
  clipping.paragraphIndex = item["para"] | UINT16_MAX;
  clipping.timestamp = item["created_at"];
  clipping.layoutSignature = item["layout_signature"];
  clipping.syncRevision = item["revision"];
  clipping.pendingUpload = false;
  if (!item["start_offset"].isNull() || !item["end_offset"].isNull()) {
    if (!item["start_offset"].is<uint32_t>() || !item["end_offset"].is<uint32_t>()) return false;
    clipping.startOffset = item["start_offset"];
    clipping.endOffset = item["end_offset"];
    if (clipping.startOffset >= clipping.endOffset || clipping.endOffset == UINT32_MAX) return false;
  }
  snprintf(clipping.chapterTitle, sizeof(clipping.chapterTitle), "%s", item["chapter"] | "");
  clipping.chapterTitle[utf8SafeTruncateBuffer(clipping.chapterTitle, strlen(clipping.chapterTitle))] = '\0';
  text.assign(quote.c_str(), quote.size());
  clipping.textLength = static_cast<uint16_t>(text.size());
  return true;
}
// The optional extension is intentionally outside sync_version=2. An older
// server returns 404 on its first GET; ordinary clipping sync remains valid.
bool syncNativeNoteAnchors(freeink::SecureHttpClient& http, const std::string& url, char* buffer, JsonDocument& doc) {
  const std::string anchorUrl = url + "/anchors";
  int status = 0;
  if (!request(http, anchorUrl + "?cursor=0", buffer, doc, false, &status)) {
    if (status == 404 && http.responseComplete()) {
      LOG_INF("CLIP", "Server does not support optional quote context v1");
      return true;
    }
    return false;
  }
  if (!doc["anchor_version"].is<unsigned>() || doc["anchor_version"].as<unsigned>() != 1) return false;

  const std::string sidecar = CLIPPINGS.contextAnchorPath();
  if (sidecar.empty()) return false;
  std::string quote;
  for (size_t index = 0; index < CLIPPINGS.clippingCount(); ++index) {
    const Clipping* clipping = CLIPPINGS.clippingAt(index);
    if (!clipping || clipping->textLength < ybxnote::kMinQuoteBytes || clipping->textLength > ybxnote::kMaxQuoteBytes)
      continue;
    if (!CLIPPINGS.readClippingText(index, quote)) return false;
    ybxnote::Context context;
    const auto state = ybxnote::contextstore::read(sidecar, clipping->id, quote, context);
    if (state == ybxnote::contextstore::Status::Invalid) {
      LOG_ERR("CLIP", "Cannot send damaged local quote context");
      return false;
    }
    // An edit delivered by normal clipping v2 can invalidate the old
    // captured context under the same ID. Ignore that stale anchor until
    // explicit recapture, without failing primary sync or overwriting it.
    if (state != ybxnote::contextstore::Status::Found) continue;

    doc.clear();
    doc["anchor_version"] = 1;
    doc["id"] = clipping->id;
    doc["text"] = quote.c_str();
    doc["prefix"] = context.prefix.c_str();
    doc["suffix"] = context.suffix.c_str();
    if (!request(http, anchorUrl, buffer, doc, true)) return false;
  }

  // A stable, independent revision cursor. No writes to ClippingStore V4,
  // no outbox mutation and no change to the normal v2 pagination cursor.
  uint64_t cursor = 0;
  do {
    if (!request(http, anchorUrl + "?cursor=" + std::to_string(cursor), buffer, doc)) return false;
    if (!doc["anchor_version"].is<unsigned>() || doc["anchor_version"].as<unsigned>() != 1 ||
        !doc["cursor"].is<uint64_t>() || !doc["more"].is<bool>() || !doc["items"].is<JsonArray>())
      return false;
    const uint64_t next = doc["cursor"];
    const JsonArrayConst items = doc["items"].as<JsonArrayConst>();
    if (items.size() > 1 || next < cursor || (items.size() && next <= cursor) ||
        (doc["more"].as<bool>() && !items.size()))
      return false;

    for (JsonObjectConst item : items) {
      const JsonString id = item["id"].as<JsonString>();
      const JsonString text = item["text"].as<JsonString>();
      const JsonString prefix = item["prefix"].as<JsonString>();
      const JsonString suffix = item["suffix"].as<JsonString>();
      if (id.isNull() || id.size() != 32 || text.isNull() || text.size() < ybxnote::kMinQuoteBytes ||
          text.size() > ybxnote::kMaxQuoteBytes || prefix.isNull() || suffix.isNull() ||
          prefix.size() > ybxnote::kMaxContextBytes || suffix.size() > ybxnote::kMaxContextBytes ||
          (!prefix.size() && !suffix.size()) || !item["revision"].is<uint64_t>())
        return false;
      const std::string remoteId(id.c_str(), id.size());
      const std::string remoteText(text.c_str(), text.size());
      const ybxnote::Context remoteContext{std::string(prefix.c_str(), prefix.size()),
                                           std::string(suffix.c_str(), suffix.size())};
      bool locallyPresent = false;
      for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
        const Clipping* clipping = CLIPPINGS.clippingAt(i);
        if (!clipping || remoteId != clipping->id) continue;
        if (!CLIPPINGS.readClippingText(i, quote)) return false;
        if (quote != remoteText) return false;  // never accept a changed quote under the same ID
        locallyPresent = true;
        break;
      }
      if (locallyPresent && !ybxnote::contextstore::save(sidecar, remoteId, remoteText, remoteContext)) return false;
    }
    cursor = next;
  } while (doc["more"].as<bool>());
  return true;
}

}  // namespace

bool clippingSync::run(const std::string& bookPath, const std::string& documentHash) {
  // Gate before loading files, allocating buffers, or contacting the server.
  if (!KOREADER_STORE.getSyncClippings()) return true;
  if (!KOREADER_STORE.hasCredentials()) return false;
  if (!CLIPPINGS.loadForBook(bookPath, "", "", "epub")) return false;
  struct Unload {
    ~Unload() { CLIPPINGS.unload(); }
  } unload;

#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
  const auto releaseScratch = [](char* buffer) { buildscratch::release(reinterpret_cast<uint8_t*>(buffer)); };
  std::unique_ptr<char[], decltype(releaseScratch)> buffer(reinterpret_cast<char*>(buildscratch::claim(BUFFER_SIZE)),
                                                           releaseScratch);
#else
  auto buffer = makeUniqueNoThrow<char[]>(BUFFER_SIZE);
#endif
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  const auto heap = HalMemory::getDefaultHeap();
  if (!buffer || !http || heap.freeBytes < 40000 || heap.largestBlockBytes < 20000) {
    LOG_ERR("CLIP", "Insufficient heap for clipping sync");
    return false;
  }
  const std::string url = KOREADER_STORE.getBaseUrl() + "/api/v1/clippings/" + documentHash;
  // Clippings carry the account KOSync password hash and optional bearer
  // credentials. Never send either over HTTP or unverified TLS; use the same
  // clock + CA/hostname verification already enforced by YBX PluginHttp.
  if (!tlspolicy::prepareHttpsClient(*http, url)) {
    LOG_ERR("CLIP", "Verified HTTPS is required for clipping sync");
    return false;
  }
  JsonDocument doc;
  // Probe the extension before any upload, even when the user explicitly enabled it.
  if (!request(*http, url + "?cursor=0&limit=1&format=reader", buffer.get(), doc) ||
      doc["sync_version"].as<unsigned>() != 2) {
    LOG_ERR("CLIP", "Clipping sync requires a compatible crosspoint-sync server");
    return false;
  }
  if (!CLIPPINGS.prepareSync()) return false;

  uint32_t deletionOffset = 0;
  char deletedId[65];
  while (true) {
    if (!CLIPPINGS.nextDeletion(deletionOffset, deletedId)) return false;
    if (!deletedId[0]) break;
    doc.clear();
    JsonObject item = doc["items"].to<JsonArray>().add<JsonObject>();
    item["id"] = deletedId;
    item["deleted"] = 1;
    if (!request(*http, url, buffer.get(), doc, true)) return false;
  }
  if (!CLIPPINGS.finishDeletions()) return false;

  std::string text;
  for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
    const Clipping& clipping = *CLIPPINGS.clippingAt(i);
    if (!clipping.pendingUpload) continue;
    if (!CLIPPINGS.readClippingText(i, text)) return false;
    doc.clear();
    encode(doc["items"].to<JsonArray>().add<JsonObject>(), clipping, text);
    if (!request(*http, url, buffer.get(), doc, true)) return false;
  }
  if (!CLIPPINGS.finishUploads()) return false;

  // ponytail: rescan one book per manual sync; persist an account-scoped cursor if histories grow large.
  uint64_t cursor = 0;
  do {
    if (!request(*http, url + "?limit=1&format=reader&cursor=" + std::to_string(cursor), buffer.get(), doc))
      return false;
    if (!doc["cursor"].is<uint64_t>() || !doc["more"].is<bool>() || !doc["items"].is<JsonArray>()) return false;
    const uint64_t next = doc["cursor"];
    const JsonArrayConst items = doc["items"].as<JsonArrayConst>();
    if (items.size() > 1 || next < cursor || ((!items.isNull() && items.size() > 0) && next <= cursor) ||
        (doc["more"].as<bool>() && items.size() == 0)) {
      LOG_ERR("CLIP", "Invalid clipping pagination");
      return false;
    }
    for (const JsonObjectConst item : items) {
      Clipping clipping;
      bool deleted = false;
      if (!decode(item, clipping, text, deleted)) {
        LOG_ERR("CLIP", "Invalid remote clipping");
        return false;
      }
      if (!CLIPPINGS.applyRemote(clipping, text, deleted)) return false;
    }
    cursor = next;
  } while (doc["more"].as<bool>());
  // Optional extension only for generated Native Notes; EPUB, arbitrary MD
  // and historical servers retain their exact existing clipping protocol.
  if (ybxdocument::isManagedNote(bookPath)) return syncNativeNoteAnchors(*http, url, buffer.get(), doc);
  return true;
}
