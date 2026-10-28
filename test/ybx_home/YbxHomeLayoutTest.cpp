#include <gtest/gtest.h>

#include <set>

#include "util/YbxHomeLayout.h"

namespace fui = freeink::ui;

namespace {
bool contains(fui::Rect outer, fui::Rect inner) {
  return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}
bool overlaps(fui::Rect a, fui::Rect b) {
  return a.x < b.right() && a.right() > b.x && a.y < b.bottom() && a.bottom() > b.y;
}
}  // namespace

TEST(YbxHomeLayout, EveryActionFitsAllPanelRotationsAndSourceStates) {
  for (const auto panel : {fui::Size{480, 800}, fui::Size{528, 792}}) {
    for (int orientation = 0; orientation < 4; ++orientation) {
      const bool landscape = orientation % 2;
      const int w = landscape ? panel.height : panel.width;
      const int h = landscape ? panel.width : panel.height;
      // Home hints move to each physical edge as the panel rotates.
      const int x = orientation == 1 ? 40 : 0;
      const int y = orientation == 2 ? 40 : 0;
      const fui::Rect safe{static_cast<int16_t>(20 + x), static_cast<int16_t>(y),
                           static_cast<int16_t>(w - 40 - (landscape ? 40 : 0)),
                           static_cast<int16_t>(h - (landscape ? 0 : 40))};
      for (bool catalogs : {false, true}) {
        for (bool recent : {false, true}) {
          for (int body : {24, 28, 32}) {
            const auto ui = ybxhome::layout(safe, body, 24, catalogs, recent);
            ASSERT_TRUE(contains(safe, ui.header));
            ASSERT_TRUE(contains(safe, ui.hero));
            EXPECT_FALSE(overlaps(ui.header, ui.hero));
            ASSERT_TRUE(contains(safe, ui.statistics));
            EXPECT_FALSE(overlaps(ui.hero, ui.statistics));
            if (recent) EXPECT_FALSE(overlaps(ui.statistics, ui.recent));
            // Hero has room for label, two title lines, author and progress.
            EXPECT_GE(ui.hero.height, 24 + 6 + 2 * body + 24 + 24 + 20);
            for (int i = 0; i < ui.actionCount; ++i) {
              ASSERT_TRUE(contains(safe, ui.actions[i]));
              EXPECT_FALSE(overlaps(ui.hero, ui.actions[i]));
              EXPECT_FALSE(overlaps(ui.statistics, ui.actions[i]));
              for (int j = i + 1; j < ui.actionCount; ++j) EXPECT_FALSE(overlaps(ui.actions[i], ui.actions[j]));
              if (recent) EXPECT_FALSE(overlaps(ui.recent, ui.actions[i]));
            }
            if (recent) {
              ASSERT_TRUE(contains(safe, ui.recent));
              EXPECT_FALSE(overlaps(ui.hero, ui.recent));
              // Two recent slots retain a cover plus two small title lines.
              EXPECT_GE(ui.recent.height, 3 * 24 + 30);
            }
          }
        }
      }
    }
  }
}

TEST(YbxHomeNavigation, AllBooksAndLegacyActionsAreReachableExactlyOnce) {
  for (int books = 0; books <= ybxhome::MAX_BOOKS; ++books) {
    for (bool catalogs : {false, true}) {
      const int count = ybxhome::visibleCount(books, catalogs);
      const int first = ybxhome::actionForPosition(books, catalogs, 0);
      int current = first;
      std::set<int> visited;
      for (int i = 0; i < count; ++i) {
        EXPECT_TRUE(visited.insert(current).second);
        const int next = ybxhome::moveSelection(current, books, catalogs, 1);
        EXPECT_EQ(ybxhome::moveSelection(next, books, catalogs, -1), current);
        current = next;
      }
      EXPECT_EQ(current, first);
      EXPECT_EQ(visited.count(ybxhome::statisticsAction(books, catalogs)), 1u);
      for (int i = 0; i < 3; ++i) EXPECT_TRUE(visited.insert(ybxhome::moreChoiceAction(books, catalogs, i)).second);
      for (int id = 0; id <= ybxhome::moreAction(books, catalogs); ++id) EXPECT_EQ(visited.count(id), 1u);
      for (int i = 0; i < 3; ++i)
        EXPECT_EQ(ybxhome::visibleSelection(ybxhome::moreChoiceAction(books, catalogs, i), books, catalogs),
                  ybxhome::moreAction(books, catalogs));
      EXPECT_EQ(ybxhome::moveSelection(-1, books, catalogs, 1), first);
      EXPECT_EQ(ybxhome::actionForPosition(books, catalogs, count), -1);
    }
  }
}

TEST(YbxStatisticsLayout, AllPanelsAndTouchActionsFitEveryOrientation) {
  for (auto panel : {fui::Size{480, 800}, fui::Size{528, 792}}) {
    for (int orientation = 0; orientation < 4; ++orientation) {
      const int w = orientation % 2 ? panel.height : panel.width;
      const int h = orientation % 2 ? panel.width : panel.height;
      const fui::Rect safe{20, 8, static_cast<int16_t>(w - 40), static_cast<int16_t>(h - 16)};
      for (int line : {24, 29, 32}) {
        auto ui = ybxhome::statisticsLayout(safe, line);
        const std::array<fui::Rect, 7> parts{ui.title,      ui.panel,      ui.finished,  ui.status,
                                             ui.actions[0], ui.actions[1], ui.actions[2]};
        for (size_t i = 0; i < parts.size(); ++i) {
          EXPECT_TRUE(contains(safe, parts[i]));
          for (size_t j = i + 1; j < parts.size(); ++j) EXPECT_FALSE(overlaps(parts[i], parts[j]));
        }
        EXPECT_GE(ui.panel.height, 4 * line + 24);
        for (auto action : ui.actions) {
          EXPECT_GE(action.height, 44);
          EXPECT_GE(action.width, 44);
        }
      }
    }
  }
}

TEST(YbxReadingChart, ZeroAndUnknownDoNotBecomePositiveBars) {
  EXPECT_EQ(ybxhome::readingBarHeight(0, 100, 240), 0);
  EXPECT_EQ(ybxhome::readingBarHeight(-1, 100, 240), 0);
  EXPECT_EQ(ybxhome::readingBarHeight(1, 1000, 24), 1);
  EXPECT_EQ(ybxhome::readingBarHeight(100, 100, 240), 240);
  EXPECT_EQ(ybxhome::readingBarHeight(INT32_MAX, INT32_MAX, 240), 240);
}
