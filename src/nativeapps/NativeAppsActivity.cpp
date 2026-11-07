#include "NativeAppsActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "AppRegistry.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void NativeAppsActivity::onEnter() {
  count_ = nativeapps::isCatalogValid() ? nativeapps::availableCount() : 0;
  if (!nativeapps::isCatalogValid()) LOG_ERR("APPS", "Invalid native-app registry");
  for (size_t i = 0; i < count_; ++i) {
    const auto* app = nativeapps::availableAt(i);
    if (!app) break;
    items_[i].label = I18n::getInstance().get(app->name);
    items_[i].subtitle = I18n::getInstance().get(app->summary);
    items_[i].actionValue = static_cast<int16_t>(i);
  }
  UiListActivity::onEnter();
}

const char* NativeAppsActivity::headerTitle() const { return tr(STR_YBX_NATIVE_APPS); }

void NativeAppsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!count_) {
    screen.target().text(
        screen.takeTop(static_cast<int16_t>(3 * screen.target().lineHeight(screen.theme().bodyText.font)),
                       screen.theme().spaceMd),
        tr(STR_YBX_APPS_EMPTY), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(count_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void NativeAppsActivity::activateIndex(int index) {
  if (index < 0 || static_cast<size_t>(index) >= count_) return;
  const auto* descriptor = nativeapps::availableAt(static_cast<size_t>(index));
  if (!descriptor) return;
  if (ESP.getMaxAllocHeap() < descriptor->minLargestFreeBlockBytes) {
    LOG_ERR("APPS", "Insufficient contiguous heap for module %s", descriptor->id);
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }

  auto activity = descriptor->create(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("APPS", "Unable to allocate native module %s", descriptor->id);
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    resetUi();
    requestUpdate();
  });
}

void NativeAppsActivity::onBackButton() { activityManager.goHome(); }
