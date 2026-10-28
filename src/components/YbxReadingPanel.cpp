#include "YbxReadingPanel.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "util/YbxHomeLayout.h"

namespace fui = freeink::ui;
void drawYbxReadingPanel(UiAppHost::UiScreen& screen, fui::Rect bounds, const ybx::ReadingSummary& data) {
  const auto ink = fui::Paint::solid(fui::Color::Black);
  auto text = screen.theme().smallText;
  const int line = screen.target().lineHeight(text.font);
  if (!data.available) {
    text.bold = true;
    screen.target().text({bounds.x, bounds.y, bounds.width, static_cast<int16_t>(line)}, tr(STR_YBX_STATS), text);
    text.bold = false;
    text.maxLines = 2;
    bounds.y += line + 6;
    bounds.height -= line + 6;
    screen.target().text(bounds, tr(STR_YBX_STATS_NO_DATA), text);
    return;
  }
  const int32_t values[] = {data.todayMinutes, data.weekMinutes, data.streakDays};
  const char* labels[] = {tr(STR_YBX_TODAY), tr(STR_YBX_WEEK), tr(STR_YBX_STREAK)};
  char value[24];
  const int col = bounds.width / 3;
  for (int i = 0; i < 3; ++i) {
    text.bold = false;
    screen.target().text(
        {static_cast<int16_t>(bounds.x + i * col), bounds.y, static_cast<int16_t>(col), static_cast<int16_t>(line)},
        labels[i], text);
    if (values[i] < 0)
      snprintf(value, sizeof(value), "--");
    else
      snprintf(value, sizeof(value), "%ld %s", static_cast<long>(values[i]),
               i == 2 ? tr(STR_YBX_DAY_SHORT) : tr(STR_YBX_MIN_SHORT));
    text.bold = true;
    screen.target().text({static_cast<int16_t>(bounds.x + i * col), static_cast<int16_t>(bounds.y + line),
                          static_cast<int16_t>(col), static_cast<int16_t>(line)},
                         value, text);
  }
  const int chartTop = bounds.y + 2 * line + 6;
  const int chartHeight = std::max(24, bounds.height - 4 * line - 12);
  int32_t maximum = 1;
  for (int32_t day : data.days) maximum = std::max(maximum, day);
  const int cell = bounds.width / 7;
  static constexpr StrId DAYS[] = {StrId::STR_YBX_MON, StrId::STR_YBX_TUE, StrId::STR_YBX_WED, StrId::STR_YBX_THU,
                                   StrId::STR_YBX_FRI, StrId::STR_YBX_SAT, StrId::STR_YBX_SUN};
  text.bold = false;
  text.align = fui::TextAlign::Center;
  for (size_t i = 0; i < 7; ++i) {
    const int x = bounds.x + i * cell;
    if (data.days[i] > 0) {
      const int height = ybxhome::readingBarHeight(data.days[i], maximum, chartHeight);
      screen.target().fill({static_cast<int16_t>(x + 4), static_cast<int16_t>(chartTop + chartHeight - height),
                            static_cast<int16_t>(cell - 8), static_cast<int16_t>(height)},
                           ink);
    } else if (data.days[i] == 0) {
      screen.target().text({static_cast<int16_t>(x), static_cast<int16_t>(chartTop + chartHeight - line),
                            static_cast<int16_t>(cell), static_cast<int16_t>(line)},
                           "0", text);
    } else
      screen.target().text({static_cast<int16_t>(x), static_cast<int16_t>(chartTop), static_cast<int16_t>(cell),
                            static_cast<int16_t>(line)},
                           "-", text);
    screen.target().text({static_cast<int16_t>(x), static_cast<int16_t>(chartTop + chartHeight),
                          static_cast<int16_t>(cell), static_cast<int16_t>(line)},
                         I18N.get(DAYS[i]), text);
  }
  char date[64];
  snprintf(date, sizeof(date), "%s %s", tr(STR_YBX_SNAPSHOT), data.date.data());
  text.align = fui::TextAlign::Left;
  screen.target().text(
      {bounds.x, static_cast<int16_t>(chartTop + chartHeight + line), bounds.width, static_cast<int16_t>(line)}, date,
      text);
}
