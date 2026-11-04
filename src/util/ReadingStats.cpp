#include "ReadingStats.h"
#include "ReadingStatsSnapshot.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char* STATS_DIR = "/.crosspoint";
constexpr const char* LEGACY_STATE = "/.crosspoint/reading-stats.bin";
constexpr const char* LEGACY_PENDING_STATE = "/.crosspoint/reading-stats.tmp";
constexpr const char* LEGACY_BOOKS = "/.crosspoint/reading-books.bin";
constexpr const char* LEGACY_PENDING_BOOKS = "/.crosspoint/reading-books.tmp";

struct SdSnapshotIO {
  bool exists(const char* path) { return Storage.exists(path); }

  bool read(const char* path, std::string& bytes) {
    return Storage.readFileToString("RSTA", path, readingstats::snapshot::MAX_BYTES, bytes);
  }

  bool writeSync(const char* path, const std::string& bytes) {
    HalFile file;
    if (!Storage.openFileForWrite("RSTA", path, file)) return false;
    const bool written = file.write(bytes.data(), bytes.size()) == bytes.size();
    if (written) file.flush();  // SdFat flush; close must also succeed.
    const bool closed = file.close();
    return written && closed;
  }

  bool remove(const char* path) { return Storage.remove(path); }
  bool rename(const char* from, const char* to) { return Storage.rename(from, to); }
};

enum class LegacyStatus { Valid, Missing, Corrupt };

bool readLegacyState(const char* path, readingstats::State& out) {
  std::string bytes;
  if (!Storage.readFileToString("RSTA", path, readingstats::MAX_STATE_BYTES, bytes)) return false;
  return readingstats::parseState(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out);
}

bool readLegacyBooks(const char* path, readingstats::snapshot::Data& out) {
  std::string bytes;
  if (!Storage.readFileToString("RSTA", path, readingstats::MAX_BOOKS_BYTES, bytes)) return false;
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(bytes.data());
  // Strictly check length so a partially written table never becomes a
  // silently shortened, but otherwise valid, imported book list.
  if (bytes.size() < readingstats::BOOKS_HEADER_BYTES || bytes.size() > readingstats::MAX_BOOKS_BYTES ||
      std::memcmp(raw, "CPB1", 4) != 0 || raw[4] != 1 ||
      bytes.size() != readingstats::BOOKS_HEADER_BYTES + raw[5] * readingstats::BOOK_RECORD_BYTES)
    return false;
  return readingstats::parseBooks(raw, bytes.size(), out.books, readingstats::MAX_BOOKS, out.bookCount);
}

LegacyStatus loadLegacy(readingstats::snapshot::Data& out) {
  const bool statePrimary = Storage.exists(LEGACY_STATE);
  const bool stateTemp = Storage.exists(LEGACY_PENDING_STATE);
  readingstats::State live;
  readingstats::State pending;
  const bool haveLive = statePrimary && readLegacyState(LEGACY_STATE, live);
  const bool haveTemp = stateTemp && readLegacyState(LEGACY_PENDING_STATE, pending);
  if (haveLive && haveTemp) {
    out.state = pending.lastSessionEpoch >= live.lastSessionEpoch ? pending : live;
  } else if (haveLive) {
    out.state = live;
  } else if (haveTemp) {
    out.state = pending;
  } else if (statePrimary || stateTemp) {
    return LegacyStatus::Corrupt;
  }

  const bool booksPrimary = Storage.exists(LEGACY_BOOKS);
  const bool booksTemp = Storage.exists(LEGACY_PENDING_BOOKS);
  // Keep the original book-file precedence; do not combine partial files.
  if (booksPrimary && readLegacyBooks(LEGACY_BOOKS, out))
    return LegacyStatus::Valid;
  if (booksTemp && readLegacyBooks(LEGACY_PENDING_BOOKS, out))
    return LegacyStatus::Valid;
  if (booksPrimary || booksTemp) return LegacyStatus::Corrupt;
  return statePrimary || stateTemp ? LegacyStatus::Valid : LegacyStatus::Missing;
}

readingstats::snapshot::LoadStatus loadCombined(SdSnapshotIO& io, readingstats::snapshot::Data& out) {
  const auto status = readingstats::snapshot::load(io, out);
  if (status != readingstats::snapshot::LoadStatus::Missing) return status;
  const LegacyStatus legacy = loadLegacy(out);
  return legacy == LegacyStatus::Corrupt ? readingstats::snapshot::LoadStatus::Corrupt
         : legacy == LegacyStatus::Valid ? readingstats::snapshot::LoadStatus::Loaded
                                          : readingstats::snapshot::LoadStatus::Missing;
}

std::unique_ptr<readingstats::snapshot::Data> newSnapshot() {
  // Avoid >4KiB of book records in the firmware task stack.
  return std::unique_ptr<readingstats::snapshot::Data>(new (std::nothrow) readingstats::snapshot::Data());
}

}  // namespace

namespace readingstats {

bool loadState(State& state) {
  auto snapshotData = newSnapshot();
  if (!snapshotData) return false;
  SdSnapshotIO io;
  const auto result = loadCombined(io, *snapshotData);
  if (result != snapshot::LoadStatus::Loaded) {
    state = State{};
    return false;
  }
  state = snapshotData->state;
  return true;
}

bool loadBooks(Book* books, int capacity, int& count) {
  count = 0;
  if (!books || capacity <= 0) return false;
  auto snapshotData = newSnapshot();
  if (!snapshotData) return false;
  SdSnapshotIO io;
  if (loadCombined(io, *snapshotData) != snapshot::LoadStatus::Loaded) return false;
  count = std::min(capacity, snapshotData->bookCount);
  for (int i = 0; i < count; ++i) books[i] = snapshotData->books[i];
  return count > 0;
}

bool recordSession(const char* id, int64_t startEpoch, uint32_t durationSeconds, const std::string& bookPath) {
  if (!id || !id[0] || std::strlen(id) > MAX_ID_LENGTH || durationSeconds == 0) return false;
  if (!Storage.ensureDirectoryExists(STATS_DIR)) return false;
  auto snapshotData = newSnapshot();
  if (!snapshotData) return false;
  SdSnapshotIO io;
  const auto status = loadCombined(io, *snapshotData);
  if (status == snapshot::LoadStatus::Corrupt) {
    LOG_ERR("RSTA", "corrupt saved statistics: refusing a destructive reset");
    return false;
  }
  if (isRecorded(snapshotData->state, id)) return true;
  const uint16_t bookDay = addSession(snapshotData->state, startEpoch, durationSeconds);
  noteSession(snapshotData->state, id, startEpoch);
  if (!bookPath.empty()) {
    addBook(snapshotData->books, snapshotData->bookCount, bookPath, durationSeconds, 1, bookDay);
  }
  if (!snapshot::save(io, *snapshotData)) {
    LOG_ERR("RSTA", "atomic stats+books snapshot write failed");
    return false;
  }
  return true;
}

}  // namespace readingstats
