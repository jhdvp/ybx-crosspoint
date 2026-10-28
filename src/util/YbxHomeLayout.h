#pragma once

#include <FreeInkUICore.h>

#include <algorithm>
#include <array>

namespace ybxhome {
constexpr int MAX_BOOKS = 3;
inline int readingBarHeight(int32_t minutes, int32_t maximum, int height) {
  if (minutes <= 0 || maximum <= 0 || height <= 0) return 0;
  return static_cast<int>(
      std::min<int64_t>(height, std::max<int64_t>(1, static_cast<int64_t>(minutes) * height / maximum)));
}
struct Layout {
  freeink::ui::Rect header, hero, recent, statistics;
  std::array<freeink::ui::Rect, 5> actions{};
  int actionCount = 0;
  bool landscape = false;
};

// Preserve legacy IDs for destinations; More is the one extra action.
inline int moreAction(int books, bool catalogs) { return books + (catalogs ? 5 : 4); }
inline int statisticsAction(int books, bool catalogs) { return moreAction(books, catalogs) + 1; }
inline int visibleCount(int books, bool catalogs) { return books + (catalogs ? 4 : 3); }
inline int moreChoiceAction(int books, bool catalogs, int choice) {
  if (choice == 0) return books;
  if (choice == 1) return books + (catalogs ? 3 : 2);
  if (choice == 2) return books + (catalogs ? 4 : 3);
  return -1;
}
inline int actionForPosition(int books, bool catalogs, int position) {
  if (position < 0 || position >= visibleCount(books, catalogs)) return -1;
  if (position < books) return position;
  const int index = position - books;
  if (index == 0) return statisticsAction(books, catalogs);
  if (index == 1) return books + 1;
  if (catalogs && index == 2) return books + 2;
  return moreAction(books, catalogs);
}
inline int visibleSelection(int current, int books, bool catalogs) {
  for (int i = 0; i < visibleCount(books, catalogs); ++i)
    if (actionForPosition(books, catalogs, i) == current) return current;
  return moreAction(books, catalogs);
}
inline int moveSelection(int current, int books, bool catalogs, int direction) {
  const int count = visibleCount(books, catalogs);
  for (int pos = 0; pos < count; ++pos) {
    if (actionForPosition(books, catalogs, pos) == current)
      return actionForPosition(books, catalogs, (pos + count + direction) % count);
  }
  return actionForPosition(books, catalogs, 0);
}

inline Layout layout(freeink::ui::Rect safe, int bodyLine, int smallLine, bool catalogs, bool hasRecent = true,
                     int statusHeight = 30) {
  using freeink::ui::Rect;
  Layout out;
  constexpr int GAP = 12;
  out.landscape = safe.width > safe.height;
  out.actionCount = catalogs ? 3 : 2;
  const int columns = out.actionCount;
  const int rows = (out.actionCount + columns - 1) / columns;
  const int headerHeight = statusHeight + std::max({28, bodyLine, smallLine}) + 12;
  const int actionHeight = std::max(44, 2 * smallLine + 8);
  out.header = {safe.x, safe.y, safe.width, static_cast<int16_t>(headerHeight)};
  const int actionTop = safe.bottom() - rows * actionHeight - (rows - 1) * GAP;
  const int cellWidth = (safe.width - (columns - 1) * GAP) / columns;
  for (int i = 0; i < out.actionCount; ++i) {
    const int col = i % columns;
    const int row = i / columns;
    const int width = i == out.actionCount - 1 && col == 0 ? safe.width : cellWidth;
    out.actions[i] = {static_cast<int16_t>(safe.x + col * (cellWidth + GAP)),
                      static_cast<int16_t>(actionTop + row * (actionHeight + GAP)), static_cast<int16_t>(width),
                      static_cast<int16_t>(actionHeight)};
  }
  const int bodyTop = out.header.bottom() + GAP;
  const int bodyHeight = std::max(0, actionTop - GAP - bodyTop);
  const int statsHeight = 4 * smallLine + 48;
  if (out.landscape) {
    const int heroWidth = (safe.width - GAP) / 2;
    out.hero = {safe.x, static_cast<int16_t>(bodyTop), static_cast<int16_t>(heroWidth),
                static_cast<int16_t>(bodyHeight)};
    out.statistics = {static_cast<int16_t>(safe.x + heroWidth + GAP), static_cast<int16_t>(bodyTop),
                      static_cast<int16_t>(safe.width - heroWidth - GAP), static_cast<int16_t>(statsHeight)};
    if (hasRecent)
      out.recent = {out.statistics.x, static_cast<int16_t>(bodyTop + statsHeight + GAP), out.statistics.width,
                    static_cast<int16_t>(std::max(0, bodyHeight - statsHeight - GAP))};
  } else {
    const int heroHeight = std::min(220, bodyHeight - statsHeight - GAP - (hasRecent ? 120 + GAP : 0));
    out.hero = {safe.x, static_cast<int16_t>(bodyTop), safe.width, static_cast<int16_t>(heroHeight)};
    out.statistics = {safe.x, static_cast<int16_t>(bodyTop + heroHeight + GAP), safe.width,
                      static_cast<int16_t>(statsHeight)};
    if (hasRecent)
      out.recent = {safe.x, static_cast<int16_t>(out.statistics.bottom() + GAP), safe.width,
                    static_cast<int16_t>(std::max(0, actionTop - GAP - out.statistics.bottom() - GAP))};
  }
  return out;
}

struct StatisticsLayout {
  freeink::ui::Rect title, panel, finished, status;
  std::array<freeink::ui::Rect, 3> actions;
};
inline StatisticsLayout statisticsLayout(freeink::ui::Rect safe, int line) {
  StatisticsLayout out;
  out.title = {safe.x, safe.y, safe.width, static_cast<int16_t>(2 * line)};
  out.panel = {safe.x, static_cast<int16_t>(out.title.bottom() + 12), safe.width,
               static_cast<int16_t>(safe.height - 7 * line - 68)};
  out.finished = {safe.x, static_cast<int16_t>(out.panel.bottom() + 12), safe.width, static_cast<int16_t>(line)};
  out.status = {safe.x, static_cast<int16_t>(out.finished.bottom() + 12), safe.width, static_cast<int16_t>(2 * line)};
  const int width = (safe.width - 24) / 3;
  for (int i = 0; i < 3; ++i)
    out.actions[i] = {static_cast<int16_t>(safe.x + i * (width + 12)),
                      static_cast<int16_t>(safe.bottom() - 2 * line - 8), static_cast<int16_t>(width),
                      static_cast<int16_t>(2 * line + 8)};
  return out;
}
}  // namespace ybxhome
