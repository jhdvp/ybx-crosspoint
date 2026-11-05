#pragma once
#include <FreeInkUICore.h>

#include <array>

#include "YbxAwards.h"
namespace ybx {
inline bool awardActionEnabled(int action, const AwardsPage& page) {
  if (action >= 0 && action < 2) return true;
  if (action == 9 || action == 10) return true;
  if (action == 11) return page.totals[2] > 0;
  const size_t total = page.totals[static_cast<size_t>(page.section)];
  if (action == 3) return page.available && page.offset >= AWARDS_PAGE_SIZE;
  if (action == 4) return page.available && page.offset + page.count < total;
  return action >= 5 && action < 9 && static_cast<size_t>(action - 5) < page.count;
}
inline int moveAwardSelection(int selected, const AwardsPage& page, int direction) {
  constexpr int ring[] = {9, 10, 11, 5, 6, 7, 8, 3, 4, 0, 1};
  int start = 0;
  for (int i = 0; i < 11; ++i)
    if (ring[i] == selected) start = i;
  for (int step = 1; step <= 11; ++step) {
    int pos = (start + 11 + direction * step) % 11;
    if (pos < 0) pos += 11;
    if (awardActionEnabled(ring[pos], page)) return ring[pos];
  }
  return 1;
}
struct AwardsLayout {
  freeink::ui::Rect title, status, section;
  std::array<freeink::ui::Rect, 4> cards;
  std::array<freeink::ui::Rect, 2> paging;
  freeink::ui::Rect pageNumber;
  std::array<freeink::ui::Rect, 2> actions;
};
inline void awardsLayout(AwardsLayout& out, freeink::ui::Rect safe, int line) {
  out.title = {safe.x, safe.y, safe.width, static_cast<int16_t>(2 * line)};
  out.status = {safe.x, static_cast<int16_t>(out.title.bottom() + 4), safe.width, static_cast<int16_t>(2 * line)};
  out.section = {safe.x, static_cast<int16_t>(out.status.bottom() + 4), safe.width, static_cast<int16_t>(2 * line + 8)};
  const int footerHeight = 2 * line + 8;
  const int footerTop = safe.bottom() - footerHeight;
  const int pagerTop = footerTop - 12 - footerHeight;
  const int width = (safe.width - 24) / 3;
  const int actionWidth = (safe.width - 12) / 2;
  for (int i = 0; i < 2; ++i)
    out.actions[i] = {static_cast<int16_t>(safe.x + i * (actionWidth + 12)), static_cast<int16_t>(footerTop),
                      static_cast<int16_t>(actionWidth), static_cast<int16_t>(footerHeight)};
  out.paging[0] = {safe.x, static_cast<int16_t>(pagerTop), static_cast<int16_t>(width),
                   static_cast<int16_t>(footerHeight)};
  out.paging[1] = {static_cast<int16_t>(safe.right() - width), static_cast<int16_t>(pagerTop),
                   static_cast<int16_t>(width), static_cast<int16_t>(footerHeight)};
  out.pageNumber = {static_cast<int16_t>(safe.x + width + 12), static_cast<int16_t>(pagerTop),
                    static_cast<int16_t>(width), static_cast<int16_t>(footerHeight)};
  const int cols = safe.width > safe.height ? 4 : 2;
  const int rows = 4 / cols;
  const int top = out.section.bottom() + 8;
  const int cardWidth = (safe.width - (cols - 1) * 12) / cols;
  const int cardHeight = (pagerTop - 8 - top - (rows - 1) * 12) / rows;
  for (int i = 0; i < 4; ++i)
    out.cards[i] = {static_cast<int16_t>(safe.x + (i % cols) * (cardWidth + 12)),
                    static_cast<int16_t>(top + (i / cols) * (cardHeight + 12)), static_cast<int16_t>(cardWidth),
                    static_cast<int16_t>(cardHeight)};
}
inline AwardsLayout awardsLayout(freeink::ui::Rect safe, int line) {
  AwardsLayout out;
  awardsLayout(out, safe, line);
  return out;
}
struct AwardCardLayout {
  freeink::ui::Rect title, description, progress;
};
inline AwardCardLayout awardCardLayout(freeink::ui::Rect box, int line, bool progress) {
  AwardCardLayout out;
  box = box.inset({6, 6, 6, 6});
  const int titleLines = box.height >= 5 * line + 12 ? 2 : 1;
  out.title = {box.x, box.y, box.width, static_cast<int16_t>(titleLines * line)};
  const int reserve = progress ? line + 4 : 0;
  const int available = std::max(0, box.height - titleLines * line - 4 - reserve);
  out.description = {box.x, static_cast<int16_t>(out.title.bottom() + 4), box.width,
                     static_cast<int16_t>(std::min(2, available / line) * line)};
  if (progress)
    out.progress = {box.x, static_cast<int16_t>(box.bottom() - line), box.width, static_cast<int16_t>(line)};
  return out;
}

}  // namespace ybx
