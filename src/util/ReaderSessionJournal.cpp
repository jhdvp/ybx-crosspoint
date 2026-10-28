#include "ReaderSessionJournal.h"

#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "PluginEvents.h"

namespace {

constexpr const char* JOURNAL_PATH = "/.crosspoint/reader-session.journal";

struct JournalFiles {
  bool exists(const std::string& path) { return Storage.exists(path.c_str()); }
  bool remove(const std::string& path) { return Storage.remove(path.c_str()); }
  bool rename(const std::string& from, const std::string& to) { return Storage.rename(from.c_str(), to.c_str()); }
  bool writeComplete(const std::string& path, const std::string_view content) {
    HalFile file;
    if (!Storage.openFileForWrite("RSES", path, file)) return false;
    const bool complete =
        file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size()) == content.size();
    const bool closed = file.close();
    return complete && closed;
  }
  bool read(const std::string& path, std::string& out, const size_t cap) {
    return Storage.readFileToString("RSES", path, cap, out);
  }
};

// The KOReader document id identifies the book to the server; a session for a
// book that cannot be hashed has no position to update.
bool documentIdFor(const std::string& book, std::string& document) {
  document = KOReaderDocumentId::calculate(book);
  return document.size() == 32 && std::all_of(document.begin(), document.end(), [](const unsigned char c) {
           return std::isdigit(c) || (c >= 'a' && c <= 'f');
         });
}

}  // namespace

namespace readersession {

bool save(const Record& record) {
  if (!Storage.ensureDirectoryExists("/.crosspoint")) {
    LOG_ERR("RSES", "cannot create /.crosspoint");
    return false;
  }
  JournalFiles files;
  const bool saved = write(files, JOURNAL_PATH, record);
  if (!saved) LOG_ERR("RSES", "session checkpoint failed");
  return saved;
}

bool load(Record& record) {
  JournalFiles files;
  return read(files, JOURNAL_PATH, record);
}

bool discard() {
  JournalFiles files;
  return remove(files, JOURNAL_PATH);
}

bool queueEvent(const Record& record) {
  std::string document;
  if (!documentIdFor(record.book, document)) {
    if (!Storage.exists(record.book.c_str())) {
      LOG_ERR("RSES", "session book is gone; dropping");
      return true;
    }
    LOG_ERR("RSES", "document id unavailable; session retried on next boot");
    return false;
  }

  char startTime[24];
  char endTime[24];
  char duration[16];
  char startProgress[8];
  char endProgress[8];
  snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(record.startEpoch));
  snprintf(endTime, sizeof(endTime), "%lld",
           static_cast<long long>(record.startEpoch > 0 ? record.startEpoch + record.durationSeconds : 0));
  snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(record.durationSeconds));
  snprintf(startProgress, sizeof(startProgress), "%u", record.startProgressBp);
  snprintf(endProgress, sizeof(endProgress), "%u", record.endProgressBp);
  const pluginevents::Var vars[] = {{"book", record.book.c_str()},    {"document", document.c_str()},
                                    {"start_time", startTime},        {"end_time", endTime},
                                    {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                    {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
  return pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8, record.id);
}

bool recoverPending() {
  Record record;
  if (!load(record)) return false;
  if (!pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    discard();
    return false;
  }
  if (!queueEvent(record)) return false;
  LOG_INF("RSES", "recovered interrupted session (%lu s)", static_cast<unsigned long>(record.durationSeconds));
  // A failed delete only repeats the delivery with the same id, which the
  // server ignores, so the journal is not retried here.
  discard();
  return true;
}

}  // namespace readersession
