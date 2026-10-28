#include "ReadingStatsActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HapticFeedback.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t MAX_MANIFEST_BYTES = 8 * 1024;
constexpr const char* DASHBOARD_SOURCE = "reading-stats.v1";
constexpr const char* UNKNOWN_VALUE = "—";

constexpr int TAB_GAP = 4;
constexpr int TAB_VPADDING = 6;
constexpr int METRIC_ROW_GAP = 14;
constexpr int BOOK_ROW_GAP = 10;
constexpr int HEATMAP_CELL_MAX = 30;
constexpr int WEEK_BAR_HEIGHT = 96;
constexpr int HEATMAP_DAYS = 28;

const char* weekdayLabel(const int row) {
  static constexpr StrId DAYS[7] = {StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED,
                                    StrId::STR_STATS_THU, StrId::STR_STATS_FRI, StrId::STR_STATS_SAT,
                                    StrId::STR_STATS_SUN};
  return I18n::getInstance().get(DAYS[std::clamp(row, 0, 6)]);
}

Color heatLevel(const uint32_t seconds) {
  if (seconds < 10u * 60u) return Color::LightGray;
  if (seconds < 30u * 60u) return Color::DarkGray;
  return Color::Black;
}

}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string pluginTitle, std::string manifestPath)
    : Activity("ReadingStatsActivity", renderer, mappedInput),
      pluginTitle(std::move(pluginTitle)),
      manifestPath(std::move(manifestPath)) {}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  dashboardLoaded = loadDashboard();
  if (dashboardLoaded) {
    readingstats::loadState(state);
    bookCount = 0;
    readingstats::loadBooks(books.data(), static_cast<int>(books.size()), bookCount);
    std::sort(books.begin(), books.begin() + bookCount,
              [](const readingstats::Book& lhs, const readingstats::Book& rhs) {
                if (lhs.seconds != rhs.seconds) return lhs.seconds > rhs.seconds;
                return lhs.lastDay > rhs.lastDay;
              });
    today = readingstats::localDayOrdinal(trustedtime::trustedNow());
    clockTrusted = today != 0;
  }
  currentTab = 0;
  bookPage.store(0);
  moreRowTop.store(-1);
  moreRowHeight.store(0);
  bookPages.store(1);
  requestUpdate();
}

bool ReadingStatsActivity::loadDashboard() {
  std::string raw;
  if (!Storage.readFileToString("RSTA", manifestPath, MAX_MANIFEST_BYTES, raw)) {
    LOG_ERR("RSTA", "Dashboard manifest unreadable: %s", manifestPath.c_str());
    return false;
  }
  // The filter keeps the parsed document to the dashboard keys only; the same
  // manifest also carries browse/auth/event sections this screen never needs.
  JsonDocument filter;
  filter["dashboard"]["source"] = true;
  filter["dashboard"]["pages"][0]["title"] = true;
  filter["dashboard"]["pages"][0]["widgets"][0]["type"] = true;
  filter["dashboard"]["pages"][0]["widgets"][0]["label"] = true;
  filter["dashboard"]["pages"][0]["widgets"][0]["field"] = true;
  filter["dashboard"]["pages"][0]["widgets"][0]["scope"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    LOG_ERR("RSTA", "Dashboard manifest is not valid JSON");
    return false;
  }
  if (strcmp(doc["dashboard"]["source"] | "", DASHBOARD_SOURCE) != 0) return false;

  const auto fieldFor = [](const char* name) {
    if (name == nullptr) return Field::None;
    if (strcmp(name, "total_seconds") == 0) return Field::TotalSeconds;
    if (strcmp(name, "sessions") == 0) return Field::Sessions;
    if (strcmp(name, "active_days") == 0) return Field::ActiveDays;
    if (strcmp(name, "latest_day_seconds") == 0) return Field::LatestDaySeconds;
    if (strcmp(name, "today_seconds") == 0) return Field::TodaySeconds;
    if (strcmp(name, "week_seconds") == 0) return Field::WeekSeconds;
    if (strcmp(name, "streak_days") == 0) return Field::StreakDays;
    if (strcmp(name, "qualified_turns") == 0) return Field::Turns;
    if (strcmp(name, "seconds_per_turn") == 0) return Field::SecondsPerTurn;
    return Field::None;
  };
  const auto typeFor = [](const char* name, WidgetType& type) {
    if (name == nullptr) return false;
    if (strcmp(name, "metric") == 0) {
      type = WidgetType::Metric;
      return true;
    }
    if (strcmp(name, "week_bars") == 0) {
      type = WidgetType::WeekBars;
      return true;
    }
    if (strcmp(name, "heatmap_28") == 0) {
      type = WidgetType::Heatmap;
      return true;
    }
    if (strcmp(name, "book_list") == 0) {
      type = WidgetType::BookList;
      return true;
    }
    return false;
  };

  pagesUsed = 0;
  for (JsonVariantConst pageJson : doc["dashboard"]["pages"].as<JsonArrayConst>()) {
    if (pagesUsed >= MAX_PAGES) break;
    Page page;
    readingstats::copyText(page.title, sizeof(page.title), pageJson["title"] | "");
    for (JsonVariantConst widgetJson : pageJson["widgets"].as<JsonArrayConst>()) {
      if (page.count >= MAX_WIDGETS_PER_PAGE) break;
      WidgetType type;
      if (!typeFor(widgetJson["type"] | "", type)) continue;  // unknown widget: skip, never guess
      Widget& widget = page.widgets[page.count++];
      widget.type = type;
      widget.field = fieldFor(widgetJson["field"] | "");
      widget.currentBook = strcmp(widgetJson["scope"] | "", "current-book") == 0;
      readingstats::copyText(widget.label, sizeof(widget.label), widgetJson["label"] | "");
    }
    if (page.count == 0) continue;
    if (page.title[0] == '\0') {
      readingstats::copyText(page.title, sizeof(page.title), tr(STR_STATS_OVERVIEW));
    }
    pages[pagesUsed++] = page;
  }
  return pagesUsed > 0;
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!dashboardLoaded) return;

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int tab = tabAt(tx, ty);
    if (tab >= 0) {
      if (tab != currentTab) {
        haptic_feedback::touchAction();
        selectTab(tab);
      }
      return;
    }
    const int top = moreRowTop.load();
    const int height = moreRowHeight.load();
    if (top >= 0 && height > 0 && ty >= top && ty < top + height) {
      haptic_feedback::touchAction();
      changeBookPage(1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (moreRowTop.load() >= 0) changeBookPage(1);
    return;
  }

  buttonNavigator.onNext([this] {
    if (pagesUsed > 1) selectTab((currentTab + 1) % pagesUsed);
  });
  buttonNavigator.onPrevious([this] {
    if (pagesUsed > 1) selectTab((currentTab + pagesUsed - 1) % pagesUsed);
  });
}

ReadingStatsActivity::BodyArea ReadingStatsActivity::bodyArea() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  BodyArea body;
  body.x = safe.x + metrics.contentSidePadding;
  body.right = safe.x + safe.width - metrics.contentSidePadding;
  body.y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  body.bottom = safe.y + safe.height - metrics.verticalSpacing;
  return body;
}

int ReadingStatsActivity::tabsHeight() const { return renderer.getLineHeight(UI_10_FONT_ID) + TAB_VPADDING * 2; }

int ReadingStatsActivity::footnoteHeight() const { return renderer.getLineHeight(UI_10_FONT_ID) * 2 + 6; }

int ReadingStatsActivity::tabAt(const int x, const int y) const {
  if (pagesUsed <= 0) return -1;
  const BodyArea body = bodyArea();
  const int top = body.y;
  const int bottom = top + tabsHeight();
  if (y < top || y >= bottom || x < body.x || x >= body.right) return -1;
  const int index = (x - body.x) * pagesUsed / (body.right - body.x);
  return std::clamp(index, 0, pagesUsed - 1);
}

void ReadingStatsActivity::selectTab(const int index) {
  if (index < 0 || index >= pagesUsed || index == currentTab) return;
  currentTab = index;
  bookPage.store(0);
  moreRowTop.store(-1);
  moreRowHeight.store(0);
  requestUpdate();
}

uint16_t ReadingStatsActivity::anchorDay() const { return clockTrusted ? today : state.lastDay; }

const readingstats::Day* ReadingStatsActivity::dayData(const uint16_t ordinal) const {
  return ordinal == 0 ? nullptr : readingstats::findDay(state, ordinal);
}

const char* ReadingStatsActivity::widgetLabel(const Widget& widget) const {
  if (widget.label[0] != '\0') return widget.label;
  switch (widget.field) {
    case Field::TotalSeconds:
      return tr(STR_STATS_TOTAL_TIME);
    case Field::Sessions:
      return tr(STR_STATS_SESSIONS);
    case Field::ActiveDays:
      return tr(STR_STATS_ACTIVE_DAYS);
    case Field::LatestDaySeconds:
      return tr(STR_STATS_LAST_DAY);
    case Field::TodaySeconds:
      return tr(STR_STATS_TODAY);
    case Field::WeekSeconds:
      return tr(STR_STATS_WEEK_TOTAL);
    case Field::StreakDays:
      return tr(STR_STATS_STREAK);
    case Field::Turns:
      return tr(STR_STATS_TURNS);
    case Field::SecondsPerTurn:
      return tr(STR_STATS_PER_TURN);
    case Field::None:
      break;
  }
  return "";
}

void ReadingStatsActivity::metricValue(const Widget& widget, char* out, const size_t size) const {
  // A per-book scope needs a "current book" this screen does not have; a field
  // this firmware cannot compute stays unknown instead of showing a made-up 0.
  if (widget.currentBook) {
    snprintf(out, size, "%s", UNKNOWN_VALUE);
    return;
  }
  switch (widget.field) {
    case Field::TotalSeconds:
      formatDuration(state.totalSeconds, out, size);
      return;
    case Field::Sessions:
      snprintf(out, size, "%u", static_cast<unsigned>(state.totalSessions));
      return;
    case Field::ActiveDays:
      snprintf(out, size, "%u", static_cast<unsigned>(state.activeDays));
      return;
    case Field::LatestDaySeconds: {
      const readingstats::Day* day = dayData(state.lastDay);
      if (day == nullptr) {
        snprintf(out, size, "%s", UNKNOWN_VALUE);
      } else {
        formatDuration(day->seconds, out, size);
      }
      return;
    }
    case Field::TodaySeconds: {
      if (!clockTrusted) {
        snprintf(out, size, "%s", UNKNOWN_VALUE);
        return;
      }
      const readingstats::Day* day = dayData(today);
      formatDuration(day == nullptr ? 0 : day->seconds, out, size);
      return;
    }
    case Field::WeekSeconds: {
      const uint16_t anchor = anchorDay();
      if (anchor == 0) {
        snprintf(out, size, "%s", UNKNOWN_VALUE);
        return;
      }
      const uint16_t first = anchor >= 6 ? static_cast<uint16_t>(anchor - 6) : 1;
      formatDuration(readingstats::sumDays(state, first, anchor), out, size);
      return;
    }
    case Field::StreakDays: {
      const uint16_t anchor = anchorDay();
      if (anchor == 0) {
        snprintf(out, size, "%s", UNKNOWN_VALUE);
        return;
      }
      bool clipped = false;
      const int days = readingstats::streakDays(state, anchor, clipped);
      snprintf(out, size, "%d%s %s", days, clipped ? "+" : "", tr(STR_STATS_DAYS_SHORT));
      return;
    }
    case Field::Turns:
    case Field::SecondsPerTurn:
    case Field::None:
      snprintf(out, size, "%s", UNKNOWN_VALUE);
      return;
  }
}

void ReadingStatsActivity::formatDuration(const uint32_t seconds, char* out, const size_t size) const {
  if (seconds == 0) {
    snprintf(out, size, "%s", tr(STR_STATS_ZERO_MINUTES));
    return;
  }
  if (seconds < 60) {
    snprintf(out, size, "%s", tr(STR_STATS_LESS_THAN_MINUTE));
    return;
  }
  const uint32_t minutes = seconds / 60;
  if (minutes < 60) {
    snprintf(out, size, "%u %s", static_cast<unsigned>(minutes), tr(STR_STATS_MINUTES_SHORT));
    return;
  }
  const uint32_t hours = minutes / 60;
  const uint32_t rest = minutes % 60;
  if (rest == 0) {
    snprintf(out, size, "%u %s", static_cast<unsigned>(hours), tr(STR_STATS_HOURS_SHORT));
  } else {
    snprintf(out, size, "%u %s %02u %s", static_cast<unsigned>(hours), tr(STR_STATS_HOURS_SHORT),
             static_cast<unsigned>(rest), tr(STR_STATS_MINUTES_SHORT));
  }
}

void ReadingStatsActivity::formatDay(const uint16_t ordinal, char* out, const size_t size) const {
  if (ordinal == 0) {
    snprintf(out, size, "%s", UNKNOWN_VALUE);
    return;
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  readingstats::civilFromDays(ordinal, year, month, day);
  snprintf(out, size, "%02u.%02u", day, month);
}

void ReadingStatsActivity::formatMoment(const uint32_t epoch, char* out, const size_t size) const {
  if (epoch == 0) {
    snprintf(out, size, "%s", UNKNOWN_VALUE);
    return;
  }
  const std::time_t value = static_cast<std::time_t>(epoch);
  std::tm local{};
  if (localtime_r(&value, &local) == nullptr) {
    snprintf(out, size, "%s", UNKNOWN_VALUE);
    return;
  }
  snprintf(out, size, "%02d.%02d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_hour, local.tm_min);
}

const char* ReadingStatsActivity::bookTitle(const readingstats::Book& book, char* buffer, const size_t size) const {
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (recent.path == book.path && !recent.title.empty()) {
      readingstats::copyText(buffer, size, recent.title.c_str());
      return buffer;
    }
  }
  const char* slash = std::strrchr(book.path, '/');
  readingstats::copyText(buffer, size, slash != nullptr ? slash + 1 : book.path);
  char* dot = std::strrchr(buffer, '.');
  if (dot != nullptr && dot != buffer) *dot = '\0';
  return buffer;
}

int ReadingStatsActivity::drawTabs(const BodyArea& body) {
  const int count = pageCount();
  if (count <= 0) return body.y;
  const int width = (body.right - body.x - TAB_GAP * (count - 1)) / count;
  const int height = tabsHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = body.y + (height - lineHeight) / 2;
  for (int i = 0; i < count; i++) {
    const int x = body.x + i * (width + TAB_GAP);
    const bool selected = i == currentTab;
    if (selected) {
      renderer.fillRect(x, body.y, width, height, true);
    } else {
      renderer.drawRect(x, body.y, width, height, true);
    }
    const std::string label = renderer.truncatedText(UI_10_FONT_ID, pages[i].title, std::max(0, width - 8));
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    renderer.drawText(UI_10_FONT_ID, x + (width - textWidth) / 2, textY, label.c_str(), !selected);
  }
  return body.y + height + UITheme::getInstance().getMetrics().verticalSpacing;
}

int ReadingStatsActivity::drawWidget(const Widget& widget, const int y, const BodyArea& body) {
  switch (widget.type) {
    case WidgetType::Metric:
      return drawMetric(widget, y, body);
    case WidgetType::WeekBars:
      return drawWeekBars(y, body);
    case WidgetType::Heatmap:
      return drawHeatmap(y, body);
    case WidgetType::BookList:
      return drawBookList(y, body);
  }
  return y;
}

int ReadingStatsActivity::drawMetric(const Widget& widget, const int y, const BodyArea& body) {
  const int labelFont = UI_10_FONT_ID;
  const int valueFont = UI_12_FONT_ID;
  const int lineHeight = std::max(renderer.getLineHeight(labelFont), renderer.getLineHeight(valueFont));
  char value[48];
  metricValue(widget, value, sizeof(value));
  const int valueWidth = renderer.getTextWidth(valueFont, value, EpdFontFamily::BOLD);
  const int labelWidth = std::max(0, (body.right - body.x) - valueWidth - 12);
  const std::string label = renderer.truncatedText(labelFont, widgetLabel(widget), labelWidth);
  renderer.drawText(labelFont, body.x, y + (lineHeight - renderer.getLineHeight(labelFont)) / 2, label.c_str());
  renderer.drawText(valueFont, body.right - valueWidth, y + (lineHeight - renderer.getLineHeight(valueFont)) / 2, value,
                    true, EpdFontFamily::BOLD);
  return y + lineHeight + METRIC_ROW_GAP;
}

int ReadingStatsActivity::drawWeekBars(const int y, const BodyArea& body) {
  const int font = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  const uint16_t anchor = anchorDay();
  if (anchor == 0) {
    renderer.drawText(UI_12_FONT_ID, body.x, y, tr(STR_STATS_NO_DAY));
    return y + renderer.getLineHeight(UI_12_FONT_ID) + 12;
  }
  const int first = anchor >= 6 ? anchor - 6 : 1;
  uint32_t maxSeconds = 0;
  for (int ordinal = first; ordinal <= anchor; ordinal++) {
    if (const readingstats::Day* day = dayData(static_cast<uint16_t>(ordinal))) {
      maxSeconds = std::max(maxSeconds, day->seconds);
    }
  }
  if (maxSeconds > 0) {
    char formatted[32];
    formatDuration(maxSeconds, formatted, sizeof(formatted));
    char caption[64];
    snprintf(caption, sizeof(caption), "%s %s", tr(STR_STATS_MAX), formatted);
    renderer.drawText(font, body.right - renderer.getTextWidth(font, caption), y, caption);
  }
  const int baseline = y + lineHeight + 6 + WEEK_BAR_HEIGHT;
  const int slot = (body.right - body.x) / 7;
  const int barWidth = std::max(8, slot * 3 / 5);
  for (int i = 0; i < 7 && first + i <= anchor; i++) {
    const int ordinal = first + i;
    const int x = body.x + i * slot + (slot - barWidth) / 2;
    if (const readingstats::Day* day = dayData(static_cast<uint16_t>(ordinal))) {
      int height = 2;
      if (maxSeconds > 0) {
        const uint64_t scaled = static_cast<uint64_t>(day->seconds) * WEEK_BAR_HEIGHT / maxSeconds;
        height = std::max(2, static_cast<int>(scaled));
      }
      renderer.fillRect(x, baseline - height, barWidth, height, true);
    } else {
      // A day the device did not see stays visibly absent, not a zero bar.
      renderer.drawRect(x, baseline - 5, barWidth, 5, true);
    }
    char label[12];
    formatDay(static_cast<uint16_t>(ordinal), label, sizeof(label));
    const EpdFontFamily::Style style = ordinal == anchor ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int labelWidth = renderer.getTextWidth(font, label, style);
    renderer.drawText(font, x + (barWidth - labelWidth) / 2, baseline + 4, label, true, style);
  }
  return baseline + 4 + lineHeight + 12;
}

int ReadingStatsActivity::drawHeatmap(const int y, const BodyArea& body) {
  const int font = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  const uint16_t anchor = anchorDay();
  if (anchor == 0) {
    renderer.drawText(UI_12_FONT_ID, body.x, y, tr(STR_STATS_NO_DAY));
    return y + renderer.getLineHeight(UI_12_FONT_ID) + 12;
  }
  const int first = anchor > HEATMAP_DAYS - 1 ? anchor - (HEATMAP_DAYS - 1) : 1;
  // Weekday rows are Monday-first: 1970-01-01 (ordinal 0) was a Thursday.
  const int firstWeek = (first + 3) / 7;
  const int lastWeek = (anchor + 3) / 7;
  const int columns = lastWeek - firstWeek + 1;
  const int weekdayWidth = renderer.getTextWidth(font, weekdayLabel(0)) + 8;
  const int gap = 2;
  const int cell =
      std::clamp((body.right - body.x - weekdayWidth - gap * (columns - 1)) / columns, 10, HEATMAP_CELL_MAX);
  for (int row = 0; row < 7; row++) {
    const int rowY = y + row * (cell + gap);
    renderer.drawText(font, body.x, rowY + (cell - lineHeight) / 2, weekdayLabel(row));
  }
  for (int ordinal = first; ordinal <= anchor; ordinal++) {
    const int week = (ordinal + 3) / 7 - firstWeek;
    const int row = (ordinal + 3) % 7;
    const int x = body.x + weekdayWidth + week * (cell + gap);
    const int cellY = y + row * (cell + gap);
    if (const readingstats::Day* day = dayData(static_cast<uint16_t>(ordinal))) {
      renderer.fillRectDither(x, cellY, cell, cell, heatLevel(day->seconds));
    } else {
      renderer.drawRect(x, cellY, cell, cell, true);
    }
    if (ordinal == anchor) renderer.drawRect(x - 1, cellY - 1, cell + 2, cell + 2, 2, true);
  }

  int legendY = y + 7 * (cell + gap) + 4;
  int x = body.x;
  renderer.drawText(font, x, legendY + (10 - lineHeight) / 2, tr(STR_STATS_LEGEND_LESS));
  x += renderer.getTextWidth(font, tr(STR_STATS_LEGEND_LESS)) + 6;
  for (const Color color : {Color::LightGray, Color::DarkGray, Color::Black}) {
    renderer.fillRectDither(x, legendY, 10, 10, color);
    x += 12;
  }
  renderer.drawText(font, x + 2, legendY + (10 - lineHeight) / 2, tr(STR_STATS_LEGEND_MORE));
  x += renderer.getTextWidth(font, tr(STR_STATS_LEGEND_MORE)) + 16;
  if (x + 12 + renderer.getTextWidth(font, tr(STR_STATS_NO_DATA)) <= body.right) {
    renderer.drawRect(x, legendY, 10, 10, true);
    renderer.drawText(font, x + 14, legendY + (10 - lineHeight) / 2, tr(STR_STATS_NO_DATA));
  }
  return legendY + 10 + 12;
}

int ReadingStatsActivity::bookRowsPerPage(const int top, const int bottom) const {
  const int rowHeight = renderer.getLineHeight(UI_12_FONT_ID) + BOOK_ROW_GAP;
  return std::max(1, (bottom - top) / rowHeight);
}

int ReadingStatsActivity::bookPageCount(const int perPage) const {
  return bookCount == 0 ? 1 : (bookCount + perPage - 1) / perPage;
}

void ReadingStatsActivity::changeBookPage(const int delta) {
  const int pages = std::max(1, static_cast<int>(bookPages.load()));
  const int current = bookPage.load();
  const int next = std::clamp(current + delta, 0, pages - 1);
  if (next == current) return;
  bookPage.store(next);
  requestUpdate();
}

int ReadingStatsActivity::drawBookList(const int y, const BodyArea& body) {
  const int font = UI_12_FONT_ID;
  const int small = UI_10_FONT_ID;
  const int rowHeight = renderer.getLineHeight(font) + BOOK_ROW_GAP;
  const int smallOffset = (renderer.getLineHeight(font) - renderer.getLineHeight(small)) / 2;
  if (bookCount == 0) {
    renderer.drawText(font, body.x, y, tr(STR_STATS_BOOKS_EMPTY));
    return y + renderer.getLineHeight(font) + 12;
  }
  const int slots = bookRowsPerPage(y, body.bottom);
  // One slot stays free for the "More" row so paging never hides a book.
  const int perPage = std::max(1, slots - 1);
  const int pages = bookPageCount(perPage);
  bookPages.store(static_cast<int16_t>(std::clamp(pages, 1, 0x7FFF)));
  int page = bookPage.load();
  if (page >= pages) {
    page = pages - 1;
    bookPage.store(page);
  }

  const int first = page * perPage;
  const int shown = std::min(perPage, bookCount - first);
  for (int i = 0; i < shown; i++) {
    const readingstats::Book& book = books[first + i];
    const int rowY = y + i * rowHeight;
    char rank[8];
    snprintf(rank, sizeof(rank), "%d.", first + i + 1);
    renderer.drawText(small, body.x, rowY + smallOffset, rank);
    char duration[32];
    formatDuration(book.seconds, duration, sizeof(duration));
    const int durationWidth = renderer.getTextWidth(font, duration, EpdFontFamily::BOLD);
    const int titleX = body.x + 22;
    char title[96];
    bookTitle(book, title, sizeof(title));
    const int titleWidth = std::max(0, body.right - durationWidth - 10 - titleX);
    const std::string label = renderer.truncatedText(font, title, titleWidth);
    renderer.drawText(font, titleX, rowY, label.c_str());
    renderer.drawText(font, body.right - durationWidth, rowY, duration, true, EpdFontFamily::BOLD);
  }

  int bottom = y + shown * rowHeight;
  if (page + 1 < pages) {
    const int moreY = bottom + 2;
    const int moreHeight = rowHeight - 4;
    renderer.drawRect(body.x, moreY, body.right - body.x, moreHeight, true);
    char more[48];
    snprintf(more, sizeof(more), "%s (%d/%d)", tr(STR_STATS_MORE), page + 2, pages);
    UITheme::drawCenteredText(renderer, Rect{body.x, moreY, body.right - body.x, moreHeight}, small,
                              moreY + (moreHeight - renderer.getLineHeight(small)) / 2, more);
    moreRowTop.store(static_cast<int16_t>(std::clamp(moreY, 0, 0x7FFF)));
    moreRowHeight.store(static_cast<int16_t>(std::clamp(moreHeight, 0, 0x7FFF)));
    bottom = moreY + moreHeight;
  }
  return bottom + 8;
}

void ReadingStatsActivity::drawFootnote(const BodyArea& body) {
  const int font = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  int y = body.bottom - footnoteHeight() + 2;
  char buffer[128];
  if (state.lastSessionEpoch > 0) {
    char moment[24];
    formatMoment(state.lastSessionEpoch, moment, sizeof(moment));
    snprintf(buffer, sizeof(buffer), "%s: %s", tr(STR_STATS_LAST_SESSION), moment);
    renderer.drawText(font, body.x, y, buffer);
  } else if (!clockTrusted) {
    renderer.drawText(font, body.x, y, tr(STR_STATS_CLOCK_UNSET));
  } else if (state.lastDay > 0) {
    char day[16];
    formatDay(state.lastDay, day, sizeof(day));
    snprintf(buffer, sizeof(buffer), "%s %s", tr(STR_STATS_SHOWING_LAST_DAY), day);
    renderer.drawText(font, body.x, y, buffer);
  }
  y += lineHeight + 2;
  if (state.undatedSeconds > 0) {
    char duration[32];
    formatDuration(state.undatedSeconds, duration, sizeof(duration));
    snprintf(buffer, sizeof(buffer), "%s: %s", tr(STR_STATS_UNDATED), duration);
    renderer.drawText(font, body.x, y, buffer);
  }
}

void ReadingStatsActivity::drawEmptyState(const BodyArea& body) {
  const int titleFont = UI_12_FONT_ID;
  const int hintFont = UI_10_FONT_ID;
  const std::vector<std::string> hint =
      renderer.wrappedText(hintFont, tr(STR_STATS_EMPTY_HINT), body.right - body.x - 24, 3);
  const int titleHeight = renderer.getLineHeight(titleFont);
  const int hintHeight = static_cast<int>(hint.size()) * renderer.getLineHeight(hintFont);
  int y = body.y + ((body.bottom - body.y) - (titleHeight + 8 + hintHeight)) / 2;
  UITheme::drawCenteredText(renderer, Rect{body.x, y, body.right - body.x, titleHeight}, titleFont, y,
                            tr(STR_STATS_EMPTY));
  y += titleHeight + 8;
  for (const auto& line : hint) {
    UITheme::drawCenteredText(renderer, Rect{body.x, y, body.right - body.x, renderer.getLineHeight(hintFont)},
                              hintFont, y, line.c_str());
    y += renderer.getLineHeight(hintFont);
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  moreRowTop.store(-1);
  moreRowHeight.store(0);

  const BodyArea body = bodyArea();
  const Rect header = UITheme::getInstance().getHeaderRect(renderer, true, false);
  const char* subtitle = (dashboardLoaded && pagesUsed > 0) ? currentPageSpec().title : nullptr;
  GUI.drawHeader(renderer, header, pluginTitle.c_str(), subtitle);

  if (!dashboardLoaded) {
    const int font = UI_12_FONT_ID;
    const int lineHeight = renderer.getLineHeight(font);
    UITheme::drawCenteredText(renderer, Rect{body.x, body.y, body.right - body.x, body.bottom - body.y}, font,
                              body.y + ((body.bottom - body.y) - lineHeight) / 2, tr(STR_STATS_UNAVAILABLE));
  } else if (state.totalSessions == 0) {
    drawEmptyState(body);
  } else {
    int y = drawTabs(body);
    BodyArea widgets = body;
    widgets.y = y;
    widgets.bottom = body.bottom - footnoteHeight();
    const Page& page = currentPageSpec();
    for (int i = 0; i < page.count; i++) {
      if (y > widgets.bottom) break;
      y = drawWidget(page.widgets[i], y, widgets);
    }
    drawFootnote(body);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), moreRowTop.load() >= 0 ? tr(STR_STATS_MORE) : "",
                                            pagesUsed > 1 ? "<" : "", pagesUsed > 1 ? ">" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
