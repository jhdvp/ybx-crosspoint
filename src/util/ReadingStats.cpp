#include "ReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

constexpr const char* STATS_DIR = "/.crosspoint";
constexpr const char* STATE_PATH = "/.crosspoint/reading-stats.bin";
constexpr const char* STATE_PENDING = "/.crosspoint/reading-stats.tmp";
constexpr const char* BOOKS_PATH = "/.crosspoint/reading-books.bin";
constexpr const char* BOOKS_PENDING = "/.crosspoint/reading-books.tmp";

bool readBlob(const char* path, std::string& out, const size_t cap) {
  return Storage.readFileToString("RSTA", path, cap, out);
}

bool writeComplete(const char* path, const uint8_t* data, const size_t length) {
  HalFile file;
  if (!Storage.openFileForWrite("RSTA", path, file)) return false;
  const bool written = file.write(data, length) == length;
  const bool closed = file.close();
  return written && closed;
}

bool loadStateFile(const char* path, readingstats::State& state) {
  std::string raw;
  if (!readBlob(path, raw, readingstats::MAX_STATE_BYTES)) return false;
  return readingstats::parseState(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), state);
}

bool saveState(const readingstats::State& state) {
  if (!Storage.ensureDirectoryExists(STATS_DIR)) return false;
  uint8_t buffer[readingstats::MAX_STATE_BYTES];
  const size_t length = readingstats::serializeState(state, buffer, sizeof(buffer));
  if (length == 0 || !writeComplete(STATE_PENDING, buffer, length)) return false;
  return Storage.replaceFile(STATE_PENDING, STATE_PATH);
}

struct BookScan {
  int count = 0;
  int matchIndex = -1;
  int evictIndex = -1;
  readingstats::Book match;
};

// Reads the header and records of the live books file. Returns false when the
// file is missing or unusable, leaving count at 0 so the caller rebuilds it.
bool scanBooks(const std::string& wantedPath, BookScan& scan) {
  char wanted[readingstats::MAX_BOOK_PATH];
  readingstats::copyText(wanted, sizeof(wanted), wantedPath.c_str());
  HalFile file;
  if (!Storage.openFileForRead("RSTA", BOOKS_PATH, file)) return false;
  uint8_t header[readingstats::BOOKS_HEADER_BYTES];
  if (file.read(header, sizeof(header)) != sizeof(header)) return false;
  if (std::memcmp(header, "CPB1", 4) != 0 || header[4] != 1) return false;
  const int stored = std::min<int>(header[5], readingstats::MAX_BOOKS);
  uint16_t oldest = UINT16_MAX;
  uint8_t raw[readingstats::BOOK_RECORD_BYTES];
  for (int i = 0; i < stored; i++) {
    if (file.read(raw, sizeof(raw)) != sizeof(raw)) break;
    readingstats::Book book;
    if (!readingstats::parseBookRecord(raw, sizeof(raw), book)) break;
    if (scan.matchIndex < 0 && std::strcmp(wanted, book.path) == 0) {
      scan.matchIndex = i;
      scan.match = book;
    }
    if (book.lastDay < oldest) {
      oldest = book.lastDay;
      scan.evictIndex = i;
    }
    scan.count = i + 1;
  }
  return true;
}

// Rewrites the book table with one entry updated or added. The header is
// written last through a seek, so a short copy cannot claim records that were
// never written.
bool updateBooks(const std::string& path, const uint32_t seconds, const uint16_t lastDay) {
  if (path.empty() || seconds == 0) return true;
  if (!Storage.ensureDirectoryExists(STATS_DIR)) return false;

  BookScan scan;
  const bool scanned = scanBooks(path, scan);

  readingstats::Book updated;
  int writeIndex = scan.matchIndex;
  if (scan.matchIndex >= 0) {
    updated = scan.match;
  } else {
    readingstats::copyText(updated.path, sizeof(updated.path), path.c_str());
    if (scan.count < readingstats::MAX_BOOKS) {
      writeIndex = scan.count;
    } else {
      writeIndex = scan.evictIndex >= 0 ? scan.evictIndex : 0;
    }
  }
  updated.seconds = readingstats::saturatingAdd(updated.seconds, seconds);
  updated.sessions = readingstats::saturatingAdd16(updated.sessions, 1);
  if (lastDay > updated.lastDay) updated.lastDay = lastDay;

  HalFile input;
  const bool haveInput = scanned && Storage.openFileForRead("RSTA", BOOKS_PATH, input);
  if (haveInput) {
    uint8_t skipped[readingstats::BOOKS_HEADER_BYTES];
    if (input.read(skipped, sizeof(skipped)) != sizeof(skipped)) return false;
  }

  HalFile output;
  if (!Storage.openFileForWrite("RSTA", BOOKS_PENDING, output)) return false;
  const uint8_t empty[readingstats::BOOKS_HEADER_BYTES] = {};
  bool ok = output.write(empty, sizeof(empty)) == sizeof(empty);
  int written = 0;
  uint8_t raw[readingstats::BOOK_RECORD_BYTES];
  for (int i = 0; i < scan.count && ok; i++) {
    if (i == writeIndex) {
      ok = readingstats::serializeBookRecord(updated, raw, sizeof(raw)) == sizeof(raw);
    } else {
      ok = haveInput && input.read(raw, sizeof(raw)) == sizeof(raw);
    }
    ok = ok && output.write(raw, sizeof(raw)) == sizeof(raw);
    if (ok) written++;
  }
  if (ok && writeIndex >= scan.count) {
    ok = readingstats::serializeBookRecord(updated, raw, sizeof(raw)) == sizeof(raw) &&
         output.write(raw, sizeof(raw)) == sizeof(raw);
    if (ok) written++;
  }
  if (ok) {
    uint8_t header[readingstats::BOOKS_HEADER_BYTES] = {'C', 'P', 'B', '1', 1, 0, 0, 0};
    header[5] = static_cast<uint8_t>(written);
    ok = output.seek(0) && output.write(header, sizeof(header)) == sizeof(header);
  }
  ok = output.close() && ok;
  if (!ok) {
    LOG_ERR("RSTA", "book table write failed");
    Storage.remove(BOOKS_PENDING);
    return false;
  }
  return Storage.replaceFile(BOOKS_PENDING, BOOKS_PATH);
}

bool loadBooksFile(const char* path, readingstats::Book* books, const int capacity, int& count) {
  HalFile file;
  if (!Storage.openFileForRead("RSTA", path, file)) return false;
  uint8_t header[readingstats::BOOKS_HEADER_BYTES];
  if (file.read(header, sizeof(header)) != sizeof(header)) return false;
  if (std::memcmp(header, "CPB1", 4) != 0 || header[4] != 1) return false;
  const int stored = std::min<int>(header[5], readingstats::MAX_BOOKS);
  count = 0;
  uint8_t raw[readingstats::BOOK_RECORD_BYTES];
  for (int i = 0; i < stored && count < capacity; i++) {
    if (file.read(raw, sizeof(raw)) != sizeof(raw)) break;
    readingstats::Book book;
    if (!readingstats::parseBookRecord(raw, sizeof(raw), book)) break;
    books[count++] = book;
  }
  return count > 0;
}

}  // namespace

namespace readingstats {

bool loadState(State& state) {
  State live;
  State pending;
  const bool haveLive = loadStateFile(STATE_PATH, live);
  const bool havePending = loadStateFile(STATE_PENDING, pending);
  if (haveLive && havePending) {
    // A leftover temp file means the replace was interrupted after the newer
    // copy was written.
    state = pending.lastSessionEpoch >= live.lastSessionEpoch ? pending : live;
    return true;
  }
  if (haveLive) {
    state = live;
    return true;
  }
  if (havePending) {
    state = pending;
    return true;
  }
  state = State{};
  return false;
}

bool loadBooks(Book* books, const int capacity, int& count) {
  count = 0;
  if (books == nullptr || capacity <= 0) return false;
  if (loadBooksFile(BOOKS_PATH, books, capacity, count)) return true;
  return loadBooksFile(BOOKS_PENDING, books, capacity, count);
}

bool recordSession(const char* id, const int64_t startEpoch, const uint32_t durationSeconds,
                   const std::string& bookPath) {
  if (durationSeconds == 0) return false;
  State state;
  loadState(state);
  // The journal retries a session with its original id; the file it wrote from
  // (or the same boot) must not make the totals grow twice.
  if (isRecorded(state, id)) return true;
  addSession(state, startEpoch, durationSeconds);
  noteSession(state, id, startEpoch);
  if (!saveState(state)) {
    LOG_ERR("RSTA", "state write failed");
    return false;
  }
  const uint16_t bookDay = startEpoch > 0 ? state.lastDay : 0;
  if (!updateBooks(bookPath, durationSeconds, bookDay)) {
    LOG_ERR("RSTA", "book table update failed");
  }
  return true;
}

}  // namespace readingstats
