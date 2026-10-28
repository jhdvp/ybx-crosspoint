#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace readersession {

// One in-progress reading session checkpoint. Written while reading and read
// at boot after a crash or power loss, so an interrupted session still reaches
// the server. The id is allocated once per session and kept in the record: a
// recovery retry or a second delivery reuses it, and the server keys its store
// by (device, event id), so a duplicate can never be counted twice.
constexpr size_t MAX_ID_LENGTH = 19;  // 8 hex nonce, '-', up to 10 decimal
constexpr size_t MAX_BOOK_PATH_LENGTH = 512;
constexpr size_t MAX_JOURNAL_BYTES = 640;
constexpr int64_t MAX_EPOCH = 4102444800LL;  // 2100-01-01, the RTC ceiling
constexpr uint32_t MAX_DURATION_SECONDS = 30u * 24u * 60u * 60u;
constexpr uint16_t MAX_PROGRESS_BP = 10000;

struct Record {
  char id[MAX_ID_LENGTH + 1] = {};
  int64_t startEpoch = 0;  // 0 when the clock was never set
  uint32_t durationSeconds = 0;
  uint16_t startProgressBp = 0;
  uint16_t endProgressBp = 0;
  std::string book;
};

namespace detail {

inline void appendNumber(std::string& out, const uint64_t value) {
  char buffer[21];
  const int length = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  out.append(buffer, static_cast<size_t>(length));
}

inline bool takeField(std::string_view& rest, std::string_view& field) {
  const size_t newline = rest.find('\n');
  if (newline == std::string_view::npos) return false;
  field = rest.substr(0, newline);
  rest.remove_prefix(newline + 1);
  return true;
}

inline bool parseNumber(const std::string_view text, const uint64_t limit, uint64_t& out) {
  if (text.empty() || text.size() > 20) return false;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (value > limit / 10 || (value == limit / 10 && digit > limit % 10)) return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

inline bool validId(const std::string_view id) {
  if (id.size() <= 9 || id.size() > MAX_ID_LENGTH || id[8] != '-') return false;
  for (size_t i = 0; i < 8; i++) {
    if (!std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
  }
  for (size_t i = 9; i < id.size(); i++) {
    if (id[i] < '0' || id[i] > '9') return false;
  }
  return true;
}

}  // namespace detail

inline bool valid(const Record& record) {
  if (!detail::validId(record.id)) return false;
  if (record.startEpoch < 0 || record.startEpoch > MAX_EPOCH) return false;
  if (record.durationSeconds == 0 || record.durationSeconds > MAX_DURATION_SECONDS) return false;
  if (record.startProgressBp > MAX_PROGRESS_BP || record.endProgressBp > MAX_PROGRESS_BP) return false;
  if (record.book.empty() || record.book.size() > MAX_BOOK_PATH_LENGTH) return false;
  return std::all_of(record.book.begin(), record.book.end(), [](const unsigned char c) { return c >= 0x20; });
}

inline std::string serialize(const Record& record) {
  std::string out;
  out.reserve(64 + record.book.size());
  out += "CPRS1\n";
  out.append(record.id);
  out += '\n';
  detail::appendNumber(out, static_cast<uint64_t>(record.startEpoch));
  out += '\n';
  detail::appendNumber(out, record.durationSeconds);
  out += '\n';
  detail::appendNumber(out, record.startProgressBp);
  out += '\n';
  detail::appendNumber(out, record.endProgressBp);
  out += '\n';
  out += record.book;
  return out;
}

inline bool parse(const std::string_view raw, Record& record) {
  std::string_view rest = raw;
  std::string_view header;
  std::string_view id;
  std::string_view start;
  std::string_view duration;
  std::string_view startProgress;
  std::string_view endProgress;
  if (!detail::takeField(rest, header) || header != "CPRS1") return false;
  if (!detail::takeField(rest, id) || !detail::takeField(rest, start) || !detail::takeField(rest, duration) ||
      !detail::takeField(rest, startProgress) || !detail::takeField(rest, endProgress)) {
    return false;
  }
  if (id.size() > MAX_ID_LENGTH) return false;

  Record out;
  std::copy(id.begin(), id.end(), out.id);
  uint64_t value = 0;
  if (!detail::parseNumber(start, static_cast<uint64_t>(MAX_EPOCH), value)) return false;
  out.startEpoch = static_cast<int64_t>(value);
  if (!detail::parseNumber(duration, MAX_DURATION_SECONDS, value)) return false;
  out.durationSeconds = static_cast<uint32_t>(value);
  if (!detail::parseNumber(startProgress, MAX_PROGRESS_BP, value)) return false;
  out.startProgressBp = static_cast<uint16_t>(value);
  if (!detail::parseNumber(endProgress, MAX_PROGRESS_BP, value)) return false;
  out.endProgressBp = static_cast<uint16_t>(value);
  out.book.assign(rest.data(), rest.size());
  if (!valid(out)) return false;
  record = std::move(out);
  return true;
}

// Writes through a complete pending file, so a power cut either keeps the
// previous checkpoint or leaves a whole newer one; read() reconciles the two.
template <typename Files>
bool write(Files& files, const std::string& path, const Record& record) {
  if (!valid(record)) return false;
  const std::string pending = path + ".pending";
  if (!files.writeComplete(pending, serialize(record))) return false;
  if (files.exists(path) && !files.remove(path)) return false;
  return files.rename(pending, path);
}

template <typename Files>
bool read(Files& files, const std::string& path, Record& record) {
  const std::string pending = path + ".pending";
  Record live;
  Record staged;
  std::string raw;
  const bool haveLive = files.exists(path) && files.read(path, raw, MAX_JOURNAL_BYTES) && parse(raw, live);
  raw.clear();
  const bool haveStaged = files.exists(pending) && files.read(pending, raw, MAX_JOURNAL_BYTES) && parse(raw, staged);
  if (haveLive && haveStaged) {
    // Never older than the live file: a checkpoint always writes pending
    // before it removes the previous journal.
    record = staged.durationSeconds >= live.durationSeconds ? staged : live;
    return true;
  }
  if (haveLive) {
    record = live;
    return true;
  }
  if (haveStaged) {
    record = staged;
    return true;
  }
  return false;
}

template <typename Files>
bool remove(Files& files, const std::string& path) {
  bool removed = true;
  if (files.exists(path)) removed = files.remove(path);
  const std::string pending = path + ".pending";
  if (files.exists(pending)) removed = files.remove(pending) && removed;
  return removed;
}

// Firmware side (ReaderSessionJournal.cpp): the checkpoint lives at
// /.crosspoint/reader-session.journal. queueEvent() builds the reader.session
// event with the record's id; recoverPending() reads the checkpoint left by a
// crash or power loss, queues it and clears it. Call recoverPending() once at
// boot after pluginevents::refreshSubscriptions().
bool save(const Record& record);
bool load(Record& record);
bool discard();
bool queueEvent(const Record& record);
bool recoverPending();

}  // namespace readersession
