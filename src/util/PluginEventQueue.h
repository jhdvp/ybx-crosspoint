#pragma once

#include <cstring>
#include <string>
#include <string_view>

namespace pluginevents {

// Compact in place: retain ordinary FIFO failures and reserve one attempt for
// the newest explicitly replaceable snapshot. No second queue buffer is needed.
template <typename IsLatest, typename Deliver>
bool drainQueue(std::string& raw, size_t& budget, IsLatest isLatest, Deliver deliver) {
  if (budget == 0) return false;
  const size_t originalSize = raw.size();
  size_t latest = std::string::npos;
  for (size_t pos = 0; pos < originalSize;) {
    const size_t nl = raw.find('\n', pos);
    const size_t end = nl == std::string::npos ? originalSize : nl;
    if (end > pos && isLatest(std::string_view(raw.data() + pos, end - pos))) latest = pos;
    pos = end + 1;
  }

  const size_t reserved = latest == std::string::npos ? 0 : 1;
  size_t ordinaryEnd = 0;
  for (size_t pos = 0; pos < originalSize && budget > reserved;) {
    const size_t nl = raw.find('\n', pos);
    const size_t end = nl == std::string::npos ? originalSize : nl;
    const std::string_view line(raw.data() + pos, end - pos);
    if (!line.empty() && !isLatest(line)) {
      --budget;
      if (!deliver(line)) break;
    }
    ordinaryEnd = end + 1;
    pos = end + 1;
  }

  bool snapshotDelivered = false;
  if (latest != std::string::npos) {
    const size_t nl = raw.find('\n', latest);
    const size_t end = nl == std::string::npos ? originalSize : nl;
    --budget;
    snapshotDelivered = deliver(std::string_view(raw.data() + latest, end - latest));
  }

  size_t kept = 0;
  for (size_t pos = 0; pos < originalSize;) {
    const size_t nl = raw.find('\n', pos);
    const size_t end = nl == std::string::npos ? originalSize : nl;
    const size_t next = end < originalSize ? end + 1 : end;
    const std::string_view line(raw.data() + pos, end - pos);
    const bool snapshot = !line.empty() && isLatest(line);
    const bool drop = line.empty() || (snapshot ? snapshotDelivered : pos < ordinaryEnd);
    if (!drop) {
      std::memmove(raw.data() + kept, raw.data() + pos, next - pos);
      kept += next - pos;
    }
    pos = end + 1;
  }
  raw.resize(kept);
  return kept != originalSize;
}

// Drops whole leading lines until the queue fits maxBytes. Never splits a
// line: when the cap would cut through an event, that event stays and older
// ones go instead. A queue whose newest single line exceeds maxBytes is
// emptied for the caller to append fresh.
inline void trimQueueHead(std::string& raw, const size_t maxBytes) {
  if (raw.size() <= maxBytes) return;
  size_t drop = 0;
  while (raw.size() - drop > maxBytes) {
    const size_t newline = raw.find('\n', drop);
    if (newline == std::string::npos) {
      drop = raw.size();
      break;
    }
    drop = newline + 1;
  }
  raw.erase(0, drop);
}

}  // namespace pluginevents
