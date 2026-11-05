#include "YbxStatisticsActivity.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/YbxReadingPanel.h"
#include "util/YbxAwardsLayout.h"
#include "util/YbxHomeLayout.h"

namespace fui = freeink::ui;
void YbxStatisticsActivity::onEnter() {
  Activity::onEnter();
  ybx::loadReadingSummary(summary);
  result = ybx::readingConnectionState();
  if (result != ybx::ReadingResult::Ok) state = State::Failed;
  resetUi();
  app.on(1, action, this);
  app.setScreen(screen, this);
  requestUpdate();
}
void YbxStatisticsActivity::onExit() {
  closeRouting();
  if (ownsWifi) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}
void YbxStatisticsActivity::screen(UiScreen& ui, void* user) { static_cast<YbxStatisticsActivity*>(user)->draw(ui); }
void YbxStatisticsActivity::action(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<YbxStatisticsActivity*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}
void YbxStatisticsActivity::refresh() {
  const auto ready = view == View::Statistics ? ybx::readingConnectionState() : ybx::awardsConnectionState();
  if (ready != ybx::ReadingResult::Ok || result == ybx::ReadingResult::NotConnected) {
    if (SETTINGS.pluginsEnabled)
      activityManager.goToPlugins(false);
    else
      activityManager.goToSettings();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ownsWifi = true;
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& value) {
                             refreshPending = !value.isCancelled;
                             childReturnFrame = true;
                             waitConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
                             waitBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
                           });
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Loading;
  }
  requestUpdateAndWait();
  const auto fetched = view == View::Statistics ? ybx::refreshReadingSummary(working)
                                                : ybx::refreshAwardsPage(awards->working, awards->shown.section, 0);
  {
    RenderLock lock(*this);
    result = fetched;
    if (view == View::Statistics) {
      if (fetched == ybx::ReadingResult::Ok || fetched == ybx::ReadingResult::StorageError) summary = working;
    } else if (fetched == ybx::ReadingResult::Ok) {
      awards->shown = awards->working;
      if (!ybx::awardActionEnabled(selection, awards->shown)) selection = awards->shown.count ? 5 : 1;
    }
    state = fetched == ybx::ReadingResult::Ok ? State::Updated : State::Failed;
  }
  requestUpdate();
}
void YbxStatisticsActivity::loop() {
  if (std::exchange(childReturnFrame, false)) return;
  const bool allowBack = !waitBackRelease;
  const bool allowConfirm = !waitConfirmRelease;
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) waitBackRelease = false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitConfirmRelease = false;
  if (refreshPending) {
    refreshPending = false;
    refresh();
    return;
  }
  if (allowBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    back();
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  const auto move = [this] {
    if (view == View::Statistics)
      selection = (selection + 1) % 3;
    else if (view == View::Awards)
      selection = ybx::moveAwardSelection(selection, awards->shown, 1);
    else
      selection = 0;
    requestUpdate();
  };
  navigator.onNext(move);
  navigator.onPrevious([this] {
    if (view == View::Statistics)
      selection = (selection + 2) % 3;
    else if (view == View::Awards)
      selection = ybx::moveAwardSelection(selection, awards->shown, -1);
    else
      selection = 0;
    requestUpdate();
  });
  const int chosen = std::exchange(pending, -1);
  if (chosen >= 0)
    activate(chosen);
  else if (allowConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    activate(selection);
}
void YbxStatisticsActivity::draw(UiScreen& ui) {
  if (view == View::Awards) {
    drawAwards(ui);
    return;
  }
  if (view == View::AwardDetail) {
    drawAwardDetail(ui);
    return;
  }
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
  const int pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int line = ui.target().lineHeight(ui.theme().smallText.font);
  const fui::Rect content{static_cast<int16_t>(safe.x + pad), static_cast<int16_t>(safe.y + 8),
                          static_cast<int16_t>(safe.width - 2 * pad), static_cast<int16_t>(safe.height - 16)};
  const auto layout = ybxhome::statisticsLayout(content, line);
  auto title = ui.theme().bodyText;
  title.bold = true;
  ui.target().text(layout.title, tr(STR_YBX_STATS), title);
  drawYbxReadingPanel(ui, layout.panel, summary);
  const char* message = statusMessage();
  auto text = ui.theme().smallText;
  text.maxLines = 2;
  auto& finished = finishedText;
  if (summary.finishedBooks < 0)
    snprintf(finished.data(), finished.size(), "%s --", tr(STR_YBX_FINISHED));
  else
    snprintf(finished.data(), finished.size(), "%s %ld", tr(STR_YBX_FINISHED),
             static_cast<long>(summary.finishedBooks));
  ui.target().text(layout.finished, finished.data(), text);
  ui.target().text(layout.status, datedStatus(message, summary.fetchedAt), text);
  drawButton(ui, layout.actions[0], 0, tr(STR_BACK));
  drawButton(ui, layout.actions[1], 1,
             (result == ybx::ReadingResult::NotConnected || result == ybx::ReadingResult::ManifestUnsupported)
                 ? tr(STR_YBX_HOME_CATALOGS)
                 : tr(STR_UPDATE));
  drawButton(ui, layout.actions[2], 2, tr(STR_YBX_AWARDS));
}
const char* YbxStatisticsActivity::statusMessage() const {
  const char* message = tr(STR_YBX_CACHED);
  if (state == State::Loading) message = tr(STR_LOADING_POPUP);
  if (state == State::Updated) message = tr(STR_YBX_UPDATED);
  if (state == State::Failed) {
    switch (result) {
      case ybx::ReadingResult::NotConnected:
        message = tr(STR_YBX_STATS_EMPTY);
        break;
      case ybx::ReadingResult::ManifestUnsupported:
        message = tr(STR_YBX_PLUGIN_UPDATE);
        break;
      case ybx::ReadingResult::StorageError:
        message = (view == View::Statistics ? tr(STR_YBX_CACHE_FAILED) : tr(STR_YBX_AWARDS_CACHE_FAILED));
        break;
      case ybx::ReadingResult::NetworkError:
      case ybx::ReadingResult::InvalidData:
        message = tr(STR_YBX_FETCH_FAILED);
        break;
      case ybx::ReadingResult::Ok:
        break;
    }
  }
  return message;
}
const char* YbxStatisticsActivity::datedStatus(const char* message, int64_t value, const char* caption) {
  if (value <= 0) return message;
  const time_t epoch = static_cast<time_t>(value);
  tm utc{};
  char stamp[24];
  if (!gmtime_r(&epoch, &utc) || !strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &utc)) return message;
  if (caption)
    snprintf(statusText.data(), statusText.size(), "%s\n%s %s %s", message, caption, stamp, tr(STR_YBX_UTC));
  else
    snprintf(statusText.data(), statusText.size(), "%s\n%s %s", message, stamp, tr(STR_YBX_UTC));
  return statusText.data();
}
void YbxStatisticsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
void YbxStatisticsActivity::openAwards() {
  auto data = makeUniqueNoThrow<AwardBuffers>();
  if (!data) {
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }
  if (ybx::loadAwardsPage(data->working, ybx::AwardSection::Received, 0)) data->shown = data->working;
  const auto ready = ybx::awardsConnectionState();
  RenderLock lock(*this);
  closeRouting();
  awards = std::move(data);
  view = View::Awards;
  selection = 1;
  result = ready;
  state = ready == ybx::ReadingResult::Ok ? State::Cached : State::Failed;
  requestUpdate();
}
void YbxStatisticsActivity::back() {
  if (view == View::Statistics) {
    activityManager.goHome();
    return;
  }
  RenderLock lock(*this);
  closeRouting();
  if (view == View::AwardDetail) {
    view = View::Awards;
    selection = 5 + detailIndex;
  } else {
    view = View::Statistics;
    awards.reset();
    selection = 2;
    state = State::Cached;
    result = ybx::ReadingResult::Ok;
  }
  requestUpdate();
}
void YbxStatisticsActivity::loadAwards(ybx::AwardSection section, size_t offset) {
  if (!awards->shown.available) {
    RenderLock lock(*this);
    closeRouting();
    ybx::resetAwardsPage(awards->shown, section, 0);
    selection = 1;
    requestUpdate();
    return;
  }
  const bool loaded = ybx::loadAwardsPage(awards->working, section, offset);
  RenderLock lock(*this);
  closeRouting();
  if (loaded) {
    awards->shown = awards->working;
    if (!ybx::awardActionEnabled(selection, awards->shown)) selection = awards->shown.count ? 5 : 1;
    state = State::Cached;
    result = ybx::ReadingResult::Ok;
  } else {
    state = State::Failed;
    result = ybx::ReadingResult::StorageError;
  }
  requestUpdate();
}
void YbxStatisticsActivity::activate(int action) {
  if (state == State::Loading) return;
  if (action == 0) {
    back();
    return;
  }
  if (view == View::AwardDetail) return;
  if (action == 1) {
    refresh();
    return;
  }
  if (view == View::Statistics) {
    if (action == 2) openAwards();
    return;
  }
  if (!ybx::awardActionEnabled(action, awards->shown)) return;
  if (action >= 9 && action <= 11) {
    loadAwards(static_cast<ybx::AwardSection>(action - 9), 0);
  } else if (action == 3)
    loadAwards(awards->shown.section, awards->shown.offset - ybx::AWARDS_PAGE_SIZE);
  else if (action == 4)
    loadAwards(awards->shown.section, awards->shown.offset + ybx::AWARDS_PAGE_SIZE);
  else if (action >= 5 && action < 9) {
    RenderLock lock(*this);
    closeRouting();
    detailIndex = action - 5;
    view = View::AwardDetail;
    selection = 0;
    requestUpdate();
  }
}
void YbxStatisticsActivity::drawButton(UiScreen& ui, fui::Rect bounds, int action, const char* label, bool enabled,
                                       bool tabSelected) {
  button.label = label;
  button.action = 1;
  button.value = action;
  const bool active = enabled && state != State::Loading;
  button.inputMask = active ? fui::InputTouch : 0;
  button.minTouchSize = 0;
  button.text = ui.theme().smallText;
  button.text.maxLines = 2;
  button.state = !active                                ? fui::StateDisabled
                 : (selection == action || tabSelected) ? fui::StateSelected
                                                        : fui::StateNormal;
  fui::button(ui.frame(), bounds, button);
}
void YbxStatisticsActivity::drawAwards(UiScreen& ui) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
  const int pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int line = ui.target().lineHeight(ui.theme().smallText.font);
  const fui::Rect content{static_cast<int16_t>(safe.x + pad), static_cast<int16_t>(safe.y + 8),
                          static_cast<int16_t>(safe.width - 2 * pad), static_cast<int16_t>(safe.height - 16)};
  ybx::awardsLayout(awards->layout, content, line);
  const auto& layout = awards->layout;
  auto text = ui.theme().smallText;
  auto title = ui.theme().bodyText;
  title.bold = true;
  ui.target().text(layout.title, tr(STR_YBX_AWARDS), title);
  const auto& page = awards->shown;
  const auto group = static_cast<size_t>(page.section);
  const int tabs = page.totals[2] > 0 ? 3 : 2;
  const int tabWidth = (layout.section.width - (tabs - 1) * 8) / tabs;
  static constexpr StrId labels[] = {StrId::STR_YBX_AWARDS_RECEIVED, StrId::STR_YBX_AWARDS_PENDING,
                                     StrId::STR_YBX_AWARDS_READER};
  for (int tab = 0; tab < tabs; ++tab) {
    if (page.available) {
      snprintf(finishedText.data(), finishedText.size(), "%s %u%s", I18N.get(labels[tab]), page.totals[tab],
               page.more[tab] ? "+" : "");
    } else {
      snprintf(finishedText.data(), finishedText.size(), "%s", I18N.get(labels[tab]));
    }
    drawButton(ui,
               {static_cast<int16_t>(layout.section.x + tab * (tabWidth + 8)), layout.section.y,
                static_cast<int16_t>(tabWidth), layout.section.height},
               9 + tab, finishedText.data(), true, group == static_cast<size_t>(tab));
  }
  text.maxLines = 2;
  const char* message = page.more[group] && state == State::Cached ? tr(STR_YBX_AWARDS_LIMIT) : statusMessage();
  ui.target().text(layout.status, datedStatus(message, page.checkedAt, tr(STR_YBX_AWARDS_CHECKED)), text);
  static constexpr auto ink = fui::Paint::solid(fui::Color::Black);
  for (size_t i = 0; i < page.count; ++i) {
    const auto box = layout.cards[i];
    const auto& award = page.cards[i];
    const auto card = ybx::awardCardLayout(box, line, award.progressPercent >= 0);
    ui.target().stroke(box, ink, selection == static_cast<int>(5 + i) ? 3 : 1);
    if (state != State::Loading) ui.frame().hit(box, 1, 5 + i, fui::InputTouch);
    text.bold = true;
    text.maxLines = card.title.height / line;
    ui.target().text(card.title, award.title.data(), text);
    text.bold = false;
    text.maxLines = card.description.height / line;
    if (card.description.height > 0) ui.target().text(card.description, award.description.data(), text);
    if (award.progressPercent >= 0) {
      char progress[12];
      snprintf(progress, sizeof(progress), "%d%%", award.progressPercent);
      text.maxLines = 1;
      ui.target().text(card.progress, progress, text);
    }
  }
  if (!page.count)
    ui.target().text(layout.cards[0], page.available ? tr(STR_YBX_AWARDS_EMPTY) : tr(STR_YBX_AWARDS_NO_DATA), text);
  drawButton(ui, layout.paging[0], 3, tr(STR_YBX_PREVIOUS), ybx::awardActionEnabled(3, page));
  drawButton(ui, layout.paging[1], 4, tr(STR_YBX_NEXT), ybx::awardActionEnabled(4, page));
  char number[24];
  snprintf(number, sizeof(number), "%u / %u", static_cast<unsigned>(page.offset / 4 + 1),
           static_cast<unsigned>(std::max<size_t>(1, (page.totals[group] + 3) / 4)));
  text.align = fui::TextAlign::Center;
  ui.target().text(layout.pageNumber, number, text);
  drawButton(ui, layout.actions[0], 0, tr(STR_BACK));
  drawButton(ui, layout.actions[1], 1,
             (result == ybx::ReadingResult::NotConnected || result == ybx::ReadingResult::ManifestUnsupported)
                 ? tr(STR_YBX_HOME_CATALOGS)
                 : tr(STR_UPDATE));
}
void YbxStatisticsActivity::drawAwardDetail(UiScreen& ui) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, false);
  const int pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int line = ui.target().lineHeight(ui.theme().smallText.font);
  const fui::Rect content{static_cast<int16_t>(safe.x + pad), static_cast<int16_t>(safe.y + 8),
                          static_cast<int16_t>(safe.width - 2 * pad), static_cast<int16_t>(safe.height - 16)};
  const auto& award = awards->shown.cards[detailIndex];
  auto title = ui.theme().bodyText;
  title.bold = true;
  title.maxLines = 2;
  ui.target().text({content.x, content.y, content.width, static_cast<int16_t>(2 * line + 12)}, award.title.data(),
                   title);
  auto text = ui.theme().smallText;
  text.maxLines = 4;
  ui.target().text(
      {content.x, static_cast<int16_t>(content.y + 2 * line + 24), content.width, static_cast<int16_t>(4 * line)},
      award.description.data(), text);
  const int64_t date = award.earnedAt > 0 ? award.earnedAt : award.observedAt;
  if (date > 0) {
    const time_t epoch = static_cast<time_t>(date);
    tm utc{};
    char stamp[24];
    if (gmtime_r(&epoch, &utc) && strftime(stamp, sizeof(stamp), "%Y-%m-%d", &utc)) {
      snprintf(statusText.data(), statusText.size(), "%s %s %s",
               award.earnedAt > 0 ? tr(STR_YBX_AWARDS_EARNED) : tr(STR_YBX_AWARDS_OBSERVED), stamp, tr(STR_YBX_UTC));
      ui.target().text(
          {content.x, static_cast<int16_t>(content.y + 6 * line + 30), content.width, static_cast<int16_t>(2 * line)},
          statusText.data(), text);
    }
  }
  drawButton(ui,
             {content.x, static_cast<int16_t>(content.bottom() - 2 * line - 8), content.width,
              static_cast<int16_t>(2 * line + 8)},
             0, tr(STR_BACK));
}
