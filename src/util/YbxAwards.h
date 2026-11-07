#pragma once
#include <StreamingJsonParser.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ybx {
enum class AwardSection : uint8_t { Received, Pending, Reader };
constexpr size_t AWARDS_PAGE_SIZE = 4;
constexpr size_t AWARDS_MAX_BYTES = 128 * 1024;
struct Award {
  std::array<char, 64> id{};
  std::array<char, 96> title{};
  std::array<char, 192> description{};
  int64_t earnedAt = 0, observedAt = 0;
  int progressPercent = -1;
};
struct AwardsPage {
  std::array<Award, AWARDS_PAGE_SIZE> cards{};
  std::array<uint16_t, 3> totals{};
  std::array<bool, 3> more{};
  AwardSection section = AwardSection::Received;
  size_t offset = 0, count = 0;
  int64_t checkedAt = 0;
  bool available = false;
};
void resetAwardsPage(AwardsPage& page, AwardSection section, size_t offset);
inline bool awardsCacheBinding(std::string_view header, std::string_view binding) {
  return binding.size() == 64 && header.size() == 65 && header[64] == '\n' && header.substr(0, 64) == binding;
}
// One bounded parser per cold SD read, allocated outside the small MCU stack.
// The output is a working buffer: publish it only after finish() succeeds.
class AwardsParser {
 public:
  AwardsParser(AwardsPage& out, AwardSection section, size_t offset);
  void feed(const char* data, size_t size);
  bool finish();

 private:
  enum class Field {
    Other,
    Yandex,
    Received,
    Pending,
    Reader,
    More,
    Checked,
    Id,
    Title,
    Description,
    Progress,
    Earned,
    Observed
  };
  static void key(void*, const char*, size_t);
  static void string(void*, const char*, size_t);
  static void number(void*, const char*, size_t);
  static void boolean(void*, bool);
  static void null(void*);
  static void objectStart(void*);
  static void objectEnd(void*);
  static void arrayStart(void*);
  static void arrayEnd(void*);
  void scalar(bool text, const char* value, size_t size);
  Award* selected();
  AwardsPage& out;
  StreamingJsonParser parser;
  Field field = Field::Other;
  int depth = 0, group = -1, arrayDepth = 0;
  bool yandex = false, more = false, rootStarted = false, rootEnded = false, bad = false;
  bool hasId = false, hasTitle = false;
  uint8_t sections = 0;
  size_t bytes = 0;
};
}  // namespace ybx
