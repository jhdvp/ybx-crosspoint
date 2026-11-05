#pragma once

#include <GfxRenderer.h>
#include <MappedInputManager.h>

#include <array>
#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/ReadingStats.h"

// Native dashboard for plugins that declare a "reading-stats.v1" dashboard in
// their device.json. Renders the bounded widget vocabulary (metric, week_bars,
// heatmap_28, book_list) over the device-local session journal; widgets and
// fields this firmware cannot compute render as "—" instead of guessed data.
class ReadingStatsActivity final : public Activity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pluginTitle,
                       std::string manifestPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class WidgetType : uint8_t { Metric, WeekBars, Heatmap, BookList };
  enum class Field : uint8_t {
    None,
    TotalSeconds,
    Sessions,
    ActiveDays,
    LatestDaySeconds,
    TodaySeconds,
    WeekSeconds,
    StreakDays,
    Turns,
    SecondsPerTurn,
  };
  struct Widget {
    WidgetType type = WidgetType::Metric;
    Field field = Field::None;
    bool currentBook = false;  // scope: current-book, which this screen cannot see
    char label[48] = {};
  };
  static constexpr int MAX_PAGES = 6;
  static constexpr int MAX_WIDGETS_PER_PAGE = 8;
  struct Page {
    char title[40] = {};
    Widget widgets[MAX_WIDGETS_PER_PAGE];
    uint8_t count = 0;
  };

  struct BodyArea {
    int x = 0;
    int y = 0;
    int right = 0;
    int bottom = 0;
  };

  bool loadDashboard();
  const Page& currentPageSpec() const { return pages[currentTab]; }
  int pageCount() const { return pagesUsed; }

  BodyArea bodyArea() const;
  int tabsHeight() const;
  int footnoteHeight() const;
  int tabAt(int x, int y) const;
  void selectTab(int index);

  uint16_t anchorDay() const;
  const readingstats::Day* dayData(uint16_t ordinal) const;

  const char* widgetLabel(const Widget& widget) const;
  void metricValue(const Widget& widget, char* out, size_t size) const;
  void formatDuration(uint32_t seconds, char* out, size_t size) const;
  void formatDay(uint16_t ordinal, char* out, size_t size) const;
  void formatMoment(uint32_t epoch, char* out, size_t size) const;
  const char* bookTitle(const readingstats::Book& book, char* buffer, size_t size) const;

  int drawTabs(const BodyArea& body);
  int drawWidget(const Widget& widget, int y, const BodyArea& body);
  int drawMetric(const Widget& widget, int y, const BodyArea& body);
  int drawWeekBars(int y, const BodyArea& body);
  int drawHeatmap(int y, const BodyArea& body);
  int drawBookList(int y, const BodyArea& body);
  void drawFootnote(const BodyArea& body);
  void drawEmptyState(const BodyArea& body);

  int bookRowsPerPage(int top, int bottom) const;
  int bookPageCount(int perPage) const;
  void changeBookPage(int delta);

  std::string pluginTitle;
  std::string manifestPath;
  std::array<Page, MAX_PAGES> pages{};
  int pagesUsed = 0;
  int currentTab = 0;

  readingstats::State state;
  std::array<readingstats::Book, readingstats::MAX_BOOKS> books{};
  int bookCount = 0;
  uint16_t today = 0;
  bool clockTrusted = false;
  bool dashboardLoaded = false;

  ButtonNavigator buttonNavigator;

  // Touch targets and paging depend on the rendered widget stack, so the
  // render task records them for the input task. bookPage is shared the same
  // way: the input task pages it, the render task clamps and draws it.
  std::atomic<int> bookPage{0};
  std::atomic<int16_t> moreRowTop{-1};
  std::atomic<int16_t> moreRowHeight{0};
  std::atomic<int16_t> bookPages{1};
};
