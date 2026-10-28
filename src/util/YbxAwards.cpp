#include "YbxAwards.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ybx {
namespace {
template <size_t N>
void copyText(std::array<char, N>& dest, const char* text, size_t size) {
  const bool clipped = size >= N;
  size_t count = std::min(clipped ? N - 4 : N - 1, size);
  if (count < size)
    while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) --count;
  std::memcpy(dest.data(), text, count);
  if (clipped) {
    std::memcpy(dest.data() + count, "...", 3);
    count += 3;
  }
  dest[count] = '\0';
}
int64_t integer(const char* data, size_t size) {
  int64_t value = 0;
  const auto parsed = std::from_chars(data, data + size, value);
  return parsed.ec == std::errc{} && parsed.ptr == data + size && value >= 0 ? value : 0;
}
}  // namespace
void resetAwardsPage(AwardsPage& page, AwardSection section, size_t offset) {
  for (auto& card : page.cards) {
    card.id.fill(0);
    card.title.fill(0);
    card.description.fill(0);
    card.earnedAt = 0;
    card.observedAt = 0;
    card.progressPercent = -1;
  }
  page.totals.fill(0);
  page.more.fill(false);
  page.section = section;
  page.offset = offset;
  page.count = 0;
  page.checkedAt = 0;
  page.available = false;
}
AwardsParser::AwardsParser(AwardsPage& value, AwardSection section, size_t offset)
    : out(value),
      parser(JsonCallbacks{this, key, string, number, boolean, null, objectStart, objectEnd, arrayStart, arrayEnd}) {
  resetAwardsPage(out, section, offset);
  bad = static_cast<unsigned>(section) > 2;
}
void AwardsParser::feed(const char* data, size_t size) {
  bytes += size;
  if (bytes > AWARDS_MAX_BYTES) {
    bad = true;
    return;
  }
  for (size_t i = 0; i < size && !bad; ++i) {
    const char c = data[i];
    if (rootEnded && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      bad = true;
      return;
    }
    parser.feed(data + i, 1);
  }
}
Award* AwardsParser::selected() {
  if (group < 0 || group != static_cast<int>(out.section)) return nullptr;
  size_t index = out.totals[group];
  return index >= out.offset && index - out.offset < AWARDS_PAGE_SIZE ? &out.cards[index - out.offset] : nullptr;
}
void AwardsParser::key(void* p, const char* value, size_t size) {
  auto& s = *static_cast<AwardsParser*>(p);
  const std::string_view text(value, size);
  static constexpr std::pair<std::string_view, Field> names[] = {{"yandex", Field::Yandex},
                                                                 {"received", Field::Received},
                                                                 {"pending", Field::Pending},
                                                                 {"ybx_reader", Field::Reader},
                                                                 {"has_more", Field::More},
                                                                 {"checked_at", Field::Checked},
                                                                 {"id", Field::Id},
                                                                 {"title", Field::Title},
                                                                 {"description", Field::Description},
                                                                 {"progress", Field::Progress},
                                                                 {"earned_at", Field::Earned},
                                                                 {"first_observed_at", Field::Observed}};
  s.field = Field::Other;
  for (const auto& item : names)
    if (item.first == text) {
      s.field = item.second;
      break;
    }
}
void AwardsParser::objectStart(void* p) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.depth == 0) {
    if (s.rootStarted) s.bad = true;
    s.rootStarted = true;
  }
  ++s.depth;
  if (s.depth == 2 && s.field == Field::Yandex) s.yandex = true;
  if (s.depth == 2 && s.field == Field::More) s.more = true;
  if (s.group >= 0 && s.depth == s.arrayDepth + 1) {
    s.hasId = false;
    s.hasTitle = false;
  }
}
void AwardsParser::objectEnd(void* p) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.group >= 0 && s.depth == s.arrayDepth + 1) {
    if (!s.hasId || !s.hasTitle || s.out.totals[s.group] >= 50) s.bad = true;
    if (s.selected()) ++s.out.count;
    ++s.out.totals[s.group];
  }
  if (s.depth == 2) {
    s.yandex = false;
    s.more = false;
  }
  if (s.depth == 1) s.rootEnded = true;
  --s.depth;
}
void AwardsParser::arrayStart(void* p) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.depth == 0) s.bad = true;
  int next = -1;
  if (s.depth == 2 && s.yandex) {
    if (s.field == Field::Received) next = 0;
    if (s.field == Field::Pending) next = 1;
  }
  if (s.depth == 1 && s.field == Field::Reader) next = 2;
  ++s.depth;
  if (next >= 0) {
    if (s.group >= 0 || (s.sections & (1u << next))) s.bad = true;
    s.sections |= 1u << next;
    s.group = next;
    s.arrayDepth = s.depth;
  }
}
void AwardsParser::arrayEnd(void* p) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.group >= 0 && s.depth == s.arrayDepth) s.group = -1;
  --s.depth;
}
void AwardsParser::string(void* p, const char* text, size_t size) {
  static_cast<AwardsParser*>(p)->scalar(true, text, size);
}
void AwardsParser::number(void* p, const char* text, size_t size) {
  static_cast<AwardsParser*>(p)->scalar(false, text, size);
}
void AwardsParser::null(void* p) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.depth == 0 || (s.group >= 0 && s.depth == s.arrayDepth)) s.bad = true;
  if (s.group >= 0 && s.depth == s.arrayDepth + 1) {
    if (s.field == Field::Id) s.hasId = false;
    if (s.field == Field::Title) s.hasTitle = false;
    if (auto* card = s.selected()) {
      if (s.field == Field::Progress) card->progressPercent = -1;
      if (s.field == Field::Earned) card->earnedAt = 0;
      if (s.field == Field::Observed) card->observedAt = 0;
    }
  }
}
void AwardsParser::boolean(void* p, bool value) {
  auto& s = *static_cast<AwardsParser*>(p);
  if (s.depth == 2 && s.more) {
    if (s.field == Field::Received) s.out.more[0] = value;
    if (s.field == Field::Pending) s.out.more[1] = value;
  } else if (s.depth == 0 || (s.group >= 0 && s.depth == s.arrayDepth))
    s.bad = true;
}
void AwardsParser::scalar(bool text, const char* value, size_t size) {
  if (depth == 0) bad = true;
  if (depth == 1 && field == Field::Checked && !text) out.checkedAt = integer(value, size);
  if (group < 0) return;
  if (depth == arrayDepth) {
    bad = true;
    return;
  }
  if (depth != arrayDepth + 1) return;
  Award* card = selected();
  switch (field) {
    case Field::Id:
      hasId = text && size > 0 && size < 64 && std::memchr(value, 0, size) == nullptr;
      if (card && hasId) copyText(card->id, value, size);
      break;
    case Field::Title:
      hasTitle = text && size > 0 && std::memchr(value, 0, size) == nullptr;
      if (card && hasTitle) copyText(card->title, value, size);
      break;
    case Field::Description:
      if (card && text) copyText(card->description, value, size);
      break;
    case Field::Earned:
      if (card && !text) card->earnedAt = integer(value, size);
      break;
    case Field::Observed:
      if (card && !text) card->observedAt = integer(value, size);
      break;
    case Field::Progress:
      if (card && !text && size < 32) {
        char token[32];
        std::memcpy(token, value, size);
        token[size] = 0;
        char* end = nullptr;
        const double progress = std::strtod(token, &end);
        if (end == token + size && std::isfinite(progress) && progress >= 0 && progress <= 1)
          card->progressPercent = static_cast<int>(std::round(progress * 100));
      }
      break;
    case Field::Other:
    case Field::Yandex:
    case Field::Received:
    case Field::Pending:
    case Field::Reader:
    case Field::More:
    case Field::Checked:
      break;
  }
}
bool AwardsParser::finish() {
  parser.feed("\n", 1);
  out.available = bytes <= AWARDS_MAX_BYTES && !bad && !parser.hasError() && rootStarted && rootEnded && depth == 0 &&
                  (sections & 3) == 3;
  return out.available;
}
}  // namespace ybx
