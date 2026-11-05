#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "ReadingStats.h"

// One committed record holds both aggregate and per-book statistics.
// Recovery selects a complete, CRC-validated generation; it never combines a
// newly saved aggregate with books from an older generation.
namespace readingstats::snapshot {

inline constexpr char PRIMARY[] = "/.crosspoint/reading-snapshot.bin";
inline constexpr char BACKUP[] = "/.crosspoint/reading-snapshot.bak";
inline constexpr char TEMP[] = "/.crosspoint/reading-snapshot.tmp";
inline constexpr size_t HEADER_BYTES = 16;
inline constexpr size_t MAX_BYTES = HEADER_BYTES + MAX_STATE_BYTES + MAX_BOOKS * BOOK_RECORD_BYTES;

struct Data {
  State state;
  Book books[MAX_BOOKS] = {};
  int bookCount = 0;
};

enum class LoadStatus { Loaded, Missing, Corrupt };
enum class Candidate { Missing, Invalid, Valid };

// A standard CRC-32, not a cryptographic signature. Only protects against
// accidental truncation / torn bytes; caller must still trust its storage.
inline uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}

inline bool serialize(const Data& data, std::string& out) {
  out.clear();
  if (data.bookCount < 0 || data.bookCount > MAX_BOOKS) return false;
  uint8_t stateBytes[MAX_STATE_BYTES];
  const size_t stateSize = serializeState(data.state, stateBytes, sizeof(stateBytes));
  if (stateSize == 0 || stateSize > UINT16_MAX) return false;
  const size_t bookSize = static_cast<size_t>(data.bookCount) * BOOK_RECORD_BYTES;
  const size_t total = HEADER_BYTES + stateSize + bookSize;
  if (total > MAX_BYTES) return false;
  out.resize(total, '\0');
  uint8_t* p = reinterpret_cast<uint8_t*>(out.data());
  std::memcpy(p, "YBS1", 4);
  p[4] = 1;
  p[5] = static_cast<uint8_t>(data.bookCount);
  p[6] = p[7] = 0;
  p[8] = static_cast<uint8_t>(stateSize & 255);
  p[9] = static_cast<uint8_t>(stateSize >> 8);
  p[10] = static_cast<uint8_t>(bookSize & 255);
  p[11] = static_cast<uint8_t>(bookSize >> 8);
  std::memcpy(p + HEADER_BYTES, stateBytes, stateSize);
  for (int i = 0; i < data.bookCount; ++i) {
    if (serializeBookRecord(data.books[i], p + HEADER_BYTES + stateSize + i * BOOK_RECORD_BYTES, BOOK_RECORD_BYTES) !=
        BOOK_RECORD_BYTES) {
      out.clear();
      return false;
    }
  }
  const uint32_t checksum = crc32(p + HEADER_BYTES, total - HEADER_BYTES);
  for (int i = 0; i < 4; ++i) p[12 + i] = static_cast<uint8_t>(checksum >> (8 * i));
  return true;
}

inline bool parse(const uint8_t* p, size_t size, Data& out) {
  if (!p || size < HEADER_BYTES || size > MAX_BYTES || std::memcmp(p, "YBS1", 4) != 0 || p[4] != 1 || p[6] != 0 ||
      p[7] != 0 || p[5] > MAX_BOOKS)
    return false;
  const uint16_t stateSize = static_cast<uint16_t>(p[8] | (p[9] << 8));
  const uint16_t bookSize = static_cast<uint16_t>(p[10] | (p[11] << 8));
  if (stateSize < STATE_HEADER_BYTES || stateSize > MAX_STATE_BYTES || bookSize != p[5] * BOOK_RECORD_BYTES ||
      size != HEADER_BYTES + stateSize + bookSize)
    return false;
  uint32_t expected = 0;
  for (int i = 0; i < 4; ++i) expected |= static_cast<uint32_t>(p[12 + i]) << (8 * i);
  if (crc32(p + HEADER_BYTES, size - HEADER_BYTES) != expected) return false;
  // The full 32-book table is 3.3 KiB: do not place it on a C3 task stack.
  auto next = std::unique_ptr<Data>(new (std::nothrow) Data());
  if (!next || !parseState(p + HEADER_BYTES, stateSize, next->state)) return false;
  for (int i = 0; i < p[5]; ++i) {
    if (!parseBookRecord(p + HEADER_BYTES + stateSize + i * BOOK_RECORD_BYTES, BOOK_RECORD_BYTES, next->books[i]))
      return false;
  }
  next->bookCount = p[5];
  out = *next;
  return true;
}

// IO requirements: exists(path), read(path, string&), writeSync(path, string),
// remove(path), rename(oldPath, newPath). This lets the exact disk transaction
// be fault-injected on a host, rather than merely testing a serializer.
template <class IO>
Candidate inspect(IO& io, const char* path, Data& out) {
  if (!io.exists(path)) return Candidate::Missing;
  std::string bytes;
  if (!io.read(path, bytes) || !parse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out))
    return Candidate::Invalid;
  return Candidate::Valid;
}

template <class IO>
LoadStatus load(IO& io, Data& out) {
  bool seen = false;
  for (const char* path : {PRIMARY, BACKUP, TEMP}) {
    const Candidate found = inspect(io, path, out);
    if (found == Candidate::Valid) return LoadStatus::Loaded;
    seen = seen || found != Candidate::Missing;
  }
  return seen ? LoadStatus::Corrupt : LoadStatus::Missing;
}

template <class IO>
bool removeIfExists(IO& io, const char* path) {
  return !io.exists(path) || io.remove(path);
}

template <class IO>
bool sameSnapshot(IO& io, const char* path, const std::string& expected) {
  std::string bytes;
  Data checked;
  return io.read(path, bytes) && bytes == expected &&
         parse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), checked);
}

template <class IO>
bool save(IO& io, const Data& data) {
  std::string bytes;
  if (!serialize(data, bytes)) return false;

  // Make the sole surviving valid generation a primary *before* touching temp.
  // It may be a previously committed backup, or a complete first-write temp.
  auto previous = std::unique_ptr<Data>(new (std::nothrow) Data());
  if (!previous) return false;
  const Candidate primary = inspect(io, PRIMARY, *previous);
  if (primary != Candidate::Valid) {
    const Candidate backup = inspect(io, BACKUP, *previous);
    const Candidate temp = inspect(io, TEMP, *previous);
    if (backup == Candidate::Valid || temp == Candidate::Valid) {
      const char* source = backup == Candidate::Valid ? BACKUP : TEMP;
      if (!removeIfExists(io, PRIMARY) || !io.rename(source, PRIMARY)) return false;
    } else if (primary != Candidate::Missing || backup != Candidate::Missing || temp != Candidate::Missing) {
      // A corrupt snapshot must not be replaced with a reset legacy state.
      return false;
    }
  }

  if (!removeIfExists(io, TEMP) || !io.writeSync(TEMP, bytes) || !sameSnapshot(io, TEMP, bytes)) return false;

  // Rotating the primary through a backup leaves a complete committed
  // generation available at every step, even if FAT rename fails.
  const bool hadPrimary = io.exists(PRIMARY);
  if (hadPrimary) {
    if (!removeIfExists(io, BACKUP) || !io.rename(PRIMARY, BACKUP)) return false;
  }
  if (!io.rename(TEMP, PRIMARY)) {
    if (hadPrimary && !io.exists(PRIMARY)) io.rename(BACKUP, PRIMARY);
    return false;
  }
  return sameSnapshot(io, PRIMARY, bytes);
}

}  // namespace readingstats::snapshot
