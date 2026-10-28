#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "util/YbxAwards.h"
#include "util/YbxAwardsLayout.h"

namespace {
std::string award(int i, std::string extra = "") {
  return "{\"id\":\"a" + std::to_string(i) + "\",\"title\":\"Award " + std::to_string(i) +
         "\",\"description\":\"Read 10 minutes\"" + extra + "}";
}
std::string list(int n, std::string extra = "") {
  std::string s = "[";
  for (int i = 0; i < n; ++i) {
    if (i) s += ",";
    s += award(i, extra);
  }
  return s + "]";
}
std::string document(int received, int pending, std::string extra = "") {
  return "{\"yandex\":{\"received\":" + list(received, extra) + ",\"pending\":" + list(pending, extra) +
         "},\"has_more\":{\"received\":true,\"pending\":false},\"ybx_reader\":[],\"checked_at\":1793196000}";
}
bool parse(std::string_view input, ybx::AwardsPage& out, ybx::AwardSection section = ybx::AwardSection::Received,
           size_t offset = 0, size_t chunk = 7) {
  ybx::AwardsParser parser(out, section, offset);
  for (size_t pos = 0; pos < input.size(); pos += chunk)
    parser.feed(input.data() + pos, std::min(chunk, input.size() - pos));
  return parser.finish();
}
bool contains(freeink::ui::Rect a, freeink::ui::Rect b) {
  return b.width > 0 && b.height > 0 && b.x >= a.x && b.y >= a.y && b.right() <= a.right() && b.bottom() <= a.bottom();
}
bool overlaps(freeink::ui::Rect a, freeink::ui::Rect b) {
  return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}
TEST(YbxAwards, StreamsFullCountsButRetainsOnlySelectedFour) {
  for (size_t chunk : {1, 7, 128}) {
    ybx::AwardsPage page;
    ASSERT_TRUE(parse(document(12, 7), page, ybx::AwardSection::Received, 4, chunk));
    EXPECT_EQ(page.totals, (std::array<uint16_t, 3>{12, 7, 0}));
    EXPECT_EQ(page.count, 4u);
    EXPECT_STREQ(page.cards[0].id.data(), "a4");
    EXPECT_STREQ(page.cards[3].id.data(), "a7");
    EXPECT_EQ(page.checkedAt, 1793196000);
    EXPECT_TRUE(page.more[0]);
    EXPECT_FALSE(page.more[1]);
  }
}
TEST(YbxAwards, MissingProgressIsUnknownAndObservedIsNotEarnedDate) {
  ybx::AwardsPage page;
  ASSERT_TRUE(parse(document(1, 0, ",\"earned_at\":null,\"first_observed_at\":1793196000"), page));
  EXPECT_EQ(page.cards[0].progressPercent, -1);
  EXPECT_EQ(page.cards[0].earnedAt, 0);
  EXPECT_EQ(page.cards[0].observedAt, 1793196000);
  for (auto [input, expected] : {std::pair{"0", 0}, std::pair{"0.25", 25}, std::pair{"1", 100}, std::pair{"-1", -1},
                                 std::pair{"1.5", -1}, std::pair{"null", -1}, std::pair{"\"0.5\"", -1}}) {
    ASSERT_TRUE(parse(document(1, 0, std::string(",\"progress\":") + input), page));
    EXPECT_EQ(page.cards[0].progressPercent, expected);
  }
}
TEST(YbxAwards, EmptyIsValidButMissingTruncatedAndWrongCardShapesAreNot) {
  ybx::AwardsPage page;
  EXPECT_TRUE(parse(document(0, 0), page));
  EXPECT_TRUE(page.available);
  EXPECT_EQ(page.count, 0u);
  for (auto input : {"{}", "[]", "{\"yandex\":{\"received\":[]}}", "{\"yandex\":{\"received\":[null],\"pending\":[]}}",
                     "{\"yandex\":{\"received\":[{\"id\":\"a\"}],\"pending\":[]}}"})
    EXPECT_FALSE(parse(input, page));
  const auto body = document(3, 2);
  EXPECT_FALSE(parse(body.substr(0, body.size() - 1), page));
  EXPECT_FALSE(parse(body + "{}", page));
  EXPECT_FALSE(parse(body + "123", page));
}
TEST(YbxAwards, LongUtf8IsTruncatedAtCharacterBoundaryWithoutLosingOtherRows) {
  std::string title;
  for (int i = 0; i < 80; ++i) title += "Я";
  std::string body = "{\"yandex\":{\"received\":[{\"id\":\"a\",\"title\":\"" + title + "\",\"description\":\"" + title +
                     title + "\"}],\"pending\":[]}}";
  ybx::AwardsPage page;
  ASSERT_TRUE(parse(body, page));
  const std::string clipped = page.cards[0].title.data();
  EXPECT_LT(clipped.size(), 96u);
  EXPECT_EQ(clipped.substr(clipped.size() - 3), "...");
  EXPECT_EQ((clipped.size() - 3) % 2, 0u);
  EXPECT_EQ(clipped.substr(0, clipped.size() - 3), title.substr(0, clipped.size() - 3));
  EXPECT_LT(std::strlen(page.cards[0].description.data()), 192u);
}
TEST(YbxAwards, MaximumApiListsAndPartialLastPage) {
  ybx::AwardsPage page;
  ASSERT_TRUE(parse(document(50, 50), page, ybx::AwardSection::Pending, 48));
  EXPECT_EQ(page.count, 2u);
  EXPECT_STREQ(page.cards[1].id.data(), "a49");
  EXPECT_FALSE(parse(document(51, 0), page));
  EXPECT_FALSE(parse(document(1, 0) + std::string(ybx::AWARDS_MAX_BYTES, ' '), page));
}
TEST(YbxAwards, ReaderSectionUsesActualRowsOnly) {
  ybx::AwardsPage page;
  const auto body = "{\"yandex\":{\"received\":[],\"pending\":[]},\"ybx_reader\":[" + award(99) + "]}";
  ASSERT_TRUE(parse(body, page, ybx::AwardSection::Reader));
  EXPECT_EQ(page.totals[2], 1);
  EXPECT_EQ(page.count, 1u);
  EXPECT_STREQ(page.cards[0].id.data(), "a99");
}
TEST(YbxAwards, NavigationSkipsUnavailablePagesAndCards) {
  ybx::AwardsPage page;
  ASSERT_TRUE(parse(document(5, 0), page));
  int selection = 5;
  std::set<int> visited;
  do {
    ASSERT_TRUE(visited.insert(selection).second);
    selection = ybx::moveAwardSelection(selection, page, 1);
  } while (selection != 5);
  EXPECT_EQ(visited, (std::set<int>{0, 1, 4, 5, 6, 7, 8, 9, 10}));
  ASSERT_TRUE(parse(document(5, 0), page, ybx::AwardSection::Received, 4));
  EXPECT_TRUE(ybx::awardActionEnabled(3, page));
  EXPECT_FALSE(ybx::awardActionEnabled(4, page));
  EXPECT_FALSE(ybx::awardActionEnabled(6, page));
}
TEST(YbxAwards, CardsPagingAndFooterFitPortraitLandscapeAndLargeFonts) {
  namespace ui = freeink::ui;
  for (auto panel : {ui::Size{480, 800}, ui::Size{528, 792}})
    for (int rotation = 0; rotation < 4; ++rotation)
      for (int line : {24, 29, 32}) {
        const ui::Rect safe{20, 8, static_cast<int16_t>((rotation % 2 ? panel.height : panel.width) - 40),
                            static_cast<int16_t>((rotation % 2 ? panel.width : panel.height) - 16)};
        auto layout = ybx::awardsLayout(safe, line);
        const std::array<ui::Rect, 12> areas{
            layout.title,     layout.status,     layout.section,    layout.cards[0],
            layout.cards[1],  layout.cards[2],   layout.cards[3],   layout.paging[0],
            layout.paging[1], layout.pageNumber, layout.actions[0], layout.actions[1],
        };
        for (size_t i = 0; i < areas.size(); ++i) {
          EXPECT_TRUE(contains(safe, areas[i]));
          for (size_t j = i + 1; j < areas.size(); ++j) EXPECT_FALSE(overlaps(areas[i], areas[j]));
        }
        for (auto card : layout.cards)
          for (bool progress : {false, true}) {
            auto text = ybx::awardCardLayout(card, line, progress);
            EXPECT_TRUE(contains(card, text.title));
            if (text.description.height) {
              EXPECT_TRUE(contains(card, text.description));
              EXPECT_FALSE(overlaps(text.title, text.description));
            }
            if (progress) {
              EXPECT_TRUE(contains(card, text.progress));
              EXPECT_FALSE(overlaps(text.description, text.progress));
            }
          }
      }
}

TEST(YbxAwards, CacheHeaderNeverAcceptsAnotherTokenOrIncompleteBinding) {
  const std::string hash(64, 'a');
  EXPECT_TRUE(ybx::awardsCacheBinding(hash + "\n", hash));
  EXPECT_FALSE(ybx::awardsCacheBinding(hash + "\n", std::string(64, 'b')));
  EXPECT_FALSE(ybx::awardsCacheBinding(hash, hash));
  EXPECT_FALSE(ybx::awardsCacheBinding(hash + "x", hash));
  EXPECT_FALSE(ybx::awardsCacheBinding("\n", ""));
}
}  // namespace
