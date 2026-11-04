#include "util/YbxNoteContextStore.h"

#include <HalStorage.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace ybxnote::contextstore {
namespace {

// Fixed wire shape. Never serialize a C++ struct (padding/endian differs).
// ID [32 lowercase hex + NUL], prefix/suffix len each uint8, 64+64 bytes
// of zero-padded UTF-8 context, FNV64 of original clipping text, then
// FNV32 checksum little endian. A server edit under the same ID must NEVER
// reuse old context to relocate a different quotation.
constexpr char kMagic[8] = {'Y','B','X','C','T','X','0','1'};
constexpr size_t kIdSize = 33;
constexpr size_t kPrefixLenOffset = kIdSize;
constexpr size_t kSuffixLenOffset = kPrefixLenOffset + 1;
constexpr size_t kPrefixOffset = kSuffixLenOffset + 1;
constexpr size_t kSuffixOffset = kPrefixOffset + kMaxContextBytes;
constexpr size_t kQuoteHashOffset = kSuffixOffset + kMaxContextBytes;
constexpr size_t kChecksumOffset = kQuoteHashOffset + 8;
constexpr size_t kRecordSize = kChecksumOffset + 4;
using Raw = std::array<uint8_t, kRecordSize>;

uint64_t quoteFingerprint(std::string_view quote) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (const unsigned char c : quote) {
    h ^= c;
    h *= UINT64_C(1099511628211);
  }
  return h;
}
uint64_t storedQuoteFingerprint(const Raw& raw) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(raw[kQuoteHashOffset + i]) << (8 * i);
  return value;
}
bool validId(std::string_view id) {
  if (id.size() != 32) return false;
  for (const unsigned char c : id)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
uint32_t checksum(const Raw& record) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kChecksumOffset; ++i) {
    h ^= record[i];
    h *= 16777619u;
  }
  return h;
}
bool validate(const Raw& raw, Context& out) {
  const std::string_view id(reinterpret_cast<const char*>(raw.data()), 32);
  if (!validId(id) || raw[32] != 0) return false;
  const size_t pl = raw[kPrefixLenOffset], sl = raw[kSuffixLenOffset];
  if (pl > kMaxContextBytes || sl > kMaxContextBytes || (pl == 0 && sl == 0)) return false;
  for (size_t i = pl; i < kMaxContextBytes; ++i) if (raw[kPrefixOffset + i]) return false;
  for (size_t i = sl; i < kMaxContextBytes; ++i) if (raw[kSuffixOffset + i]) return false;
  const uint32_t expected = checksum(raw);
  const uint32_t recorded = static_cast<uint32_t>(raw[kChecksumOffset]) |
                            (static_cast<uint32_t>(raw[kChecksumOffset+1]) << 8) |
                            (static_cast<uint32_t>(raw[kChecksumOffset+2]) << 16) |
                            (static_cast<uint32_t>(raw[kChecksumOffset+3]) << 24);
  if (recorded != expected) return false;
  Context candidate{
    std::string(reinterpret_cast<const char*>(raw.data() + kPrefixOffset), pl),
    std::string(reinterpret_cast<const char*>(raw.data() + kSuffixOffset), sl)
  };
  if (!nativeapps::notes::isValidUtf8(candidate.prefix, true) ||
      !nativeapps::notes::isValidUtf8(candidate.suffix, true)) return false;
  out = std::move(candidate);
  return true;
}
Raw encode(std::string_view id, std::string_view quote, const Context& context) {
  Raw raw{};
  std::memcpy(raw.data(), id.data(), 32);
  raw[kPrefixLenOffset] = static_cast<uint8_t>(context.prefix.size());
  raw[kSuffixLenOffset] = static_cast<uint8_t>(context.suffix.size());
  std::memcpy(raw.data() + kPrefixOffset, context.prefix.data(), context.prefix.size());
  std::memcpy(raw.data() + kSuffixOffset, context.suffix.data(), context.suffix.size());
  const uint64_t quoteHash = quoteFingerprint(quote);
  for (unsigned i = 0; i < 8; ++i)
    raw[kQuoteHashOffset + i] = static_cast<uint8_t>(quoteHash >> (i * 8));
  const uint32_t sum = checksum(raw);
  for (unsigned i = 0; i < 4; ++i)
    raw[kChecksumOffset + i] = static_cast<uint8_t>(sum >> (i * 8));
  return raw;
}

struct Scan {
  bool ok = false;
  size_t count = 0;
  bool found = false;
  Context context;
  uint64_t quoteHash = 0;
};

// Validate the ENTIRE file, not just the first matching ID. A truncated,
// partially written or conflicting file never authorizes a new highlight.
Scan scan(const std::string& path, std::string_view id) {
  HalFile file;
  if (!Storage.openFileForRead("NCTX", path, file)) return {};
  const size_t size = file.size();
  if (size < sizeof(kMagic) || size > sizeof(kMagic) + kRecordSize*kMaxRecords ||
      (size - sizeof(kMagic)) % kRecordSize != 0) {
    file.close();
    return {};
  }
  char magic[sizeof(kMagic)]{};
  if (file.read(magic, sizeof(magic)) != static_cast<int>(sizeof(magic)) ||
      std::memcmp(magic, kMagic, sizeof(kMagic))) {
    file.close();
    return {};
  }

  Scan answer{true, 0, false, {}, 0};
  answer.count = (size - sizeof(kMagic)) / kRecordSize;
  for (size_t i = 0; i < answer.count; ++i) {
    Raw raw{};
    Context candidate;
    if (file.read(raw.data(), raw.size()) != static_cast<int>(raw.size()) ||
        !validate(raw, candidate)) {
      answer.ok = false;
      break;
    }
    if (id.size() == 32 && std::memcmp(raw.data(), id.data(), 32) == 0) {
      if (answer.found) { answer.ok = false; break; }
      answer.found = true;
      answer.context = std::move(candidate);
      answer.quoteHash = storedQuoteFingerprint(raw);
    }
  }
  if (!file.close()) answer.ok = false;
  return answer;
}
bool recover(const std::string& path) {
  if (Storage.exists(path.c_str())) return true;
  const std::string backup = path + ".bak";
  if (!Storage.exists(backup.c_str())) return false;
  const auto valid = scan(backup, {});
  return valid.ok && Storage.rename(backup.c_str(), path.c_str());
}
} // namespace

Status read(const std::string& path, std::string_view id, std::string_view quote, Context& out) {
  if (path.empty() || quote.size() < kMinQuoteBytes ||
      quote.size() > kMaxQuoteBytes || !nativeapps::notes::isValidUtf8(quote, true))
    return Status::Invalid;
  if (!Storage.exists(path.c_str()) && !recover(path)) {
    // A broken orphaned backup/temporary file means we cannot assume
    // no anchor was recorded. Do not downgrade to quote-only matching.
    if (Storage.exists((path + ".bak").c_str()) ||
        Storage.exists((path + ".tmp").c_str())) return Status::Invalid;
    return Status::Missing;
  }
  // Older server/legacy clip IDs can be 64 hex or even anonymous. This
  // optional V1 sidecar can only contain locally minted 32-hex IDs.
  // Validate the file first, then preserve PR #20's quote-only fallback for
  // older entries *without* a context record. Corruption still fails closed.
  const auto result = scan(path, validId(id) ? id : std::string_view{});
  if (!result.ok) return Status::Invalid;
  if (!validId(id) || !result.found) return Status::Missing;
  // A remote clipping can change its quote under an existing ID. The old
  // context must not be reused, but this is not an SD corruption and must
  // not block the user's unrelated quote synchronization.
  if (result.quoteHash != quoteFingerprint(quote)) return Status::Stale;
  out = result.context;
  return Status::Found;
}

bool save(const std::string& path, std::string_view id, std::string_view quote, const Context& anchor) {
  if (path.empty() || !validId(id) || quote.size() < kMinQuoteBytes ||
      quote.size() > kMaxQuoteBytes || !nativeapps::notes::isValidUtf8(quote, true) ||
      anchor.prefix.size() > kMaxContextBytes || anchor.suffix.size() > kMaxContextBytes ||
      (anchor.prefix.empty() && anchor.suffix.empty()) ||
      !nativeapps::notes::isValidUtf8(anchor.prefix, true) ||
      !nativeapps::notes::isValidUtf8(anchor.suffix, true)) return false;

  const bool hasMain = Storage.exists(path.c_str()) || recover(path);
  if (!hasMain && (Storage.exists((path + ".bak").c_str()) ||
                   Storage.exists((path + ".tmp").c_str()))) return false;
  Scan previous;
  if (hasMain) {
    previous = scan(path, id);
    if (!previous.ok) return false;
    if (previous.found) return previous.quoteHash == quoteFingerprint(quote) &&
                                previous.context.prefix == anchor.prefix &&
                                previous.context.suffix == anchor.suffix;
    if (previous.count >= kMaxRecords) return false;
  }

  const std::string tmp = path + ".tmp";
  const std::string bak = path + ".bak";
  if (Storage.exists(tmp.c_str()) && !Storage.remove(tmp.c_str())) return false;

  HalFile source;
  if (hasMain && !Storage.openFileForRead("NCTX", path, source)) return false;
  HalFile dest;
  if (!Storage.openFileForWrite("NCTX", tmp, dest)) { if (source) source.close(); return false; }

  bool ok = dest.write(kMagic, sizeof(kMagic)) == sizeof(kMagic);
  if (hasMain) {
    char discarded[sizeof(kMagic)]{};
    ok = ok && source.read(discarded, sizeof(discarded)) == static_cast<int>(sizeof(discarded));
    for (size_t n = 0; ok && n < previous.count; ++n) {
      Raw rec{};
      ok = source.read(rec.data(), rec.size()) == static_cast<int>(rec.size()) &&
           dest.write(rec.data(), rec.size()) == rec.size();
    }
  }
  const Raw encoded = encode(id, quote, anchor);
  ok = ok && dest.write(encoded.data(), encoded.size()) == encoded.size();
  dest.flush();
  if (!dest.close()) ok = false;
  if (source && !source.close()) ok = false;
  if (!ok) { Storage.remove(tmp.c_str()); return false; }

  const Scan verified = scan(tmp, id);
  if (!verified.ok || !verified.found || verified.count != previous.count + 1 ||
      verified.quoteHash != quoteFingerprint(quote) ||
      verified.context.prefix != anchor.prefix || verified.context.suffix != anchor.suffix) {
    Storage.remove(tmp.c_str());
    return false;
  }

  if (hasMain) {
    if (Storage.exists(bak.c_str()) && !Storage.remove(bak.c_str())) {
      Storage.remove(tmp.c_str()); return false;
    }
    if (!Storage.rename(path.c_str(), bak.c_str())) {
      Storage.remove(tmp.c_str()); return false;
    }
  }
  if (!Storage.rename(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    if (hasMain) Storage.rename(bak.c_str(), path.c_str());
    return false;
  }
  if (hasMain && Storage.exists(bak.c_str())) Storage.remove(bak.c_str());
  return true;
}
} // namespace ybxnote::contextstore
