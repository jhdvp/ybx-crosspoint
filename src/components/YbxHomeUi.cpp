#include "YbxHomeUi.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "UITheme.h"
#include "YbxReadingPanel.h"
#include "util/BookProgress.h"
#include "util/YbxReadingData.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
constexpr int GAP = 12;
const auto INK = fui::Paint::solid(fui::Color::Black);
const auto PAPER = fui::Paint::solid(fui::Color::White);
}  // namespace

YbxHomeUi::YbxHomeUi(GfxRenderer& renderer) : UiAppHost(renderer), renderer(renderer), covers(renderer) {}

void YbxHomeUi::begin(const std::vector<RecentBook>& recent, bool hasCatalogs) {
  books = &recent;
  catalogs = hasCatalogs;
  ybx::loadReadingSummary(summary);
  // Deliberately do not begin the PSRAM cache: X3/X4 decode bounded SD thumbnails.
  resetUi();
  app.on(SELECT, &YbxHomeUi::onAction, this);
  app.setScreen(&YbxHomeUi::screenFn, this);
  for (size_t i = 0; i < books->size() && i < progress.size(); ++i) {
    progress[i] = loadBookProgress((*books)[i].path);
    if (progress[i] >= 0) snprintf(progressText[i].data(), progressText[i].size(), "%d%%", progress[i]);
  }
}

void YbxHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<YbxHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int YbxHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

int YbxHomeUi::thumbHeightFor(size_t index) const {
  return index < thumbHeights.size() ? std::max(1, thumbHeights[index]) : 1;
}

bool YbxHomeUi::takeThumbHeightChanged() { return std::exchange(thumbsChanged, false); }

void YbxHomeUi::refreshCoverPaths() {
  covers.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i)
    coverPaths[i] = UITheme::getCoverThumbPath((*books)[i].coverBmpPath, thumbHeightFor(i));
}

void YbxHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<YbxHomeUi*>(user)->draw(screen); }

void YbxHomeUi::drawCover(fui::Rect bounds, size_t index) {
  if (index >= coverPaths.size() || bounds.empty()) return;
  if (thumbHeights[index] != bounds.height) {
    thumbHeights[index] = bounds.height;
    thumbsChanged = true;
  }
  covers.paint(bounds, index, coverPaths[index]);
}

void YbxHomeUi::drawAction(UiScreen& screen, fui::Rect bounds, int value, const char* label, bool compact) {
  button = {};
  button.label = label;
  button.action = SELECT;
  button.value = value;
  button.inputMask = fui::InputTouch;
  button.minTouchSize = 0;
  button.state = selected == value ? fui::StateSelected : fui::StateNormal;
  button.text = compact ? screen.theme().smallText : screen.theme().bodyText;
  button.text.maxLines = 2;
  button.styles.explicitlySet = true;
  button.styles.normal.background = PAPER;
  button.styles.normal.foreground = INK;
  button.styles.normal.border = INK;
  button.styles.normal.borderWidth = compact ? 0 : 2;
  button.styles.selected = button.styles.normal;
  button.styles.selected.background = INK;
  button.styles.selected.foreground = PAPER;
  button.styles.active = button.styles.selected;
  fui::button(screen.frame(), bounds, button);
}

void YbxHomeUi::drawHero(UiScreen& screen, fui::Rect bounds) {
  const auto& theme = screen.theme();
  const int labelHeight = screen.target().lineHeight(theme.smallText.font);
  auto label = theme.smallText;
  screen.target().text(fui::Rect{bounds.x, bounds.y, bounds.width, static_cast<int16_t>(labelHeight)},
                       tr(STR_CONTINUE_READING), label);
  bounds.y += labelHeight + 6;
  bounds.height -= labelHeight + 6;
  if (bounds.empty()) return;
  if (books->empty()) {
    auto message = theme.bodyText;
    message.bold = true;
    message.maxLines = 2;
    screen.target().text(bounds, tr(STR_YBX_HOME_EMPTY), message);
    return;
  }
  const auto& book = books->front();
  card = {};
  card.title = book.title.empty() ? tr(STR_LIBRARY_UNKNOWN_TITLE) : book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.progressMax = progress[0] >= 0 ? 100 : 0;
  card.progress = std::max(0, progress[0]);
  card.progressLabel = progress[0] >= 0 ? progressText[0].data() : nullptr;
  card.titleText = theme.bodyText;
  card.titleText.bold = true;
  card.titleText.maxLines = 2;
  card.authorText = theme.smallText;
  card.authorText.maxLines = 1;
  card.progressText = theme.smallText;
  card.action = SELECT;
  card.value = 0;
  card.state = selected == 0 ? fui::StateSelected : fui::StateNormal;
  card.styles.explicitlySet = true;
  card.styles.normal.background = PAPER;
  card.styles.normal.foreground = INK;
  card.styles.selected = card.styles.normal;
  card.styles.selected.border = INK;
  card.styles.selected.borderWidth = 2;
  card.styles.active = card.styles.selected;
  card.padding = {6, 6, 6, 6};
  card.gap = GAP;
  card.coverSize.height = std::max(1, std::min<int>(bounds.height - 12, bounds.width / 3 * 3 / 2));
  card.coverSize.width = card.coverSize.height * 2 / 3;
  card.progressHeight = 6;
  card.centerTextOnCover = true;
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget&, fui::Rect rect, const fui::BookCardProps&, void* user) {
    static_cast<YbxHomeUi*>(user)->drawCover(rect, 0);
    return true;
  };
  fui::bookCard(screen.frame(), bounds, card);
}

void YbxHomeUi::drawRecent(UiScreen& screen, fui::Rect bounds) {
  if (books->size() < 2 || bounds.empty()) return;
  const auto& theme = screen.theme();
  const int line = screen.target().lineHeight(theme.smallText.font);
  auto label = theme.smallText;
  label.bold = true;
  screen.target().text(fui::Rect{bounds.x, bounds.y, bounds.width, static_cast<int16_t>(line)}, tr(STR_YBX_HOME_RECENT),
                       label);
  const int count = std::min<int>(ybxhome::MAX_BOOKS - 1, books->size() - 1);
  const int width = (bounds.width - (count - 1) * GAP) / count;
  const int top = bounds.y + line + 6;
  const int coverHeight = std::max(1, std::min(width * 3 / 2 - 12, bounds.height - 3 * line - 18));
  for (int i = 0; i < count; ++i) {
    const int index = i + 1;
    const int x = bounds.x + i * (width + GAP);
    const fui::Rect cover{static_cast<int16_t>(x + (width - coverHeight * 2 / 3) / 2), static_cast<int16_t>(top),
                          static_cast<int16_t>(coverHeight * 2 / 3), static_cast<int16_t>(coverHeight)};
    drawCover(cover, index);
    const auto& book = (*books)[index];
    const fui::Rect tile{static_cast<int16_t>(x), static_cast<int16_t>(top), static_cast<int16_t>(width),
                         static_cast<int16_t>(coverHeight + 2 * line + 12)};
    screen.frame().hit(tile, SELECT, index, fui::InputTouch);
    if (selected == index) screen.target().stroke(cover.inset({-3, -3, -3, -3}), INK, 2);
    auto caption = theme.smallText;
    caption.align = fui::TextAlign::Center;
    screen.target().text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top + coverHeight + 6),
                                   static_cast<int16_t>(width), static_cast<int16_t>(line)},
                         book.title.empty() ? tr(STR_LIBRARY_UNKNOWN_TITLE) : book.title.c_str(), caption);
    if (progress[index] >= 0)
      screen.target().text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(top + coverHeight + line + 10),
                                     static_cast<int16_t>(width), static_cast<int16_t>(line)},
                           progressText[index].data(), caption);
  }
}

void YbxHomeUi::draw(UiScreen& screen) {
  covers.prepare();
  const auto& theme = screen.theme();
  const bool hints = !uiTarget.deviceContext().hasTouch;
  auto safe = UITheme::getInstance().getScreenSafeArea(renderer, hints);
  const int padding = UITheme::getInstance().getMetrics().contentSidePadding;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout =
      ybxhome::layout(fui::Rect{static_cast<int16_t>(safe.x + padding), static_cast<int16_t>(safe.y),
                                static_cast<int16_t>(safe.width - 2 * padding), static_cast<int16_t>(safe.height)},
                      screen.target().lineHeight(theme.bodyText.font), screen.target().lineHeight(theme.smallText.font),
                      catalogs, books->size() > 1, metrics.topPadding + metrics.batteryBarHeight);
  auto status = UITheme::getInstance().getHeaderRect(renderer, hints);
  status.height = metrics.batteryBarHeight;
  GUI.drawHeader(renderer, status, nullptr, nullptr, false);
  const int markY = layout.header.y + metrics.topPadding + metrics.batteryBarHeight + 6;
  screen.target().fill(fui::Rect{layout.header.x, static_cast<int16_t>(markY), 28, 28}, INK, 6);
  for (int i = 0; i < 3; ++i)
    screen.target().fill(fui::Rect{static_cast<int16_t>(layout.header.x + 6), static_cast<int16_t>(markY + 7 + i * 6),
                                   static_cast<int16_t>(i == 2 ? 10 : 16), 3},
                         PAPER);
  auto brand = theme.titleText;
  brand.bold = true;
  screen.target().text(fui::Rect{static_cast<int16_t>(layout.header.x + 40), static_cast<int16_t>(markY),
                                 static_cast<int16_t>(layout.header.width - 40), 32},
                       tr(STR_YBX), brand);
  screen.target().line({layout.header.x, layout.header.bottom()}, {layout.header.right(), layout.header.bottom()}, 2,
                       INK);
  drawHero(screen, layout.hero);
  drawRecent(screen, layout.recent);
  const int statistics = ybxhome::statisticsAction(books->size(), catalogs);
  screen.frame().hit(layout.statistics, SELECT, statistics, fui::InputTouch);
  if (selected == statistics) screen.target().stroke(layout.statistics.inset({-3, -3, -3, -3}), INK, 2);
  drawYbxReadingPanel(screen, layout.statistics, summary);
  const int base = books->size();
  for (int i = 0; i < layout.actionCount; ++i) {
    const int value = ybxhome::actionForPosition(base, catalogs, base + 1 + i);
    const char* label = value == base + 1                              ? tr(STR_LIBRARY)
                        : value == ybxhome::moreAction(base, catalogs) ? tr(STR_TOOL_MORE)
                                                                       : tr(STR_YBX_HOME_CATALOGS);
    drawAction(screen, layout.actions[i], value, label, true);
  }
}
