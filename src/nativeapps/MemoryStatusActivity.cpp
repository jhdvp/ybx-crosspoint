#include "MemoryStatusActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"

namespace fui = freeink::ui;

void MemoryStatusActivity::onEnter() {
  constexpr StrId names[] = {
      StrId::STR_YBX_APP_FREE_HEAP, StrId::STR_YBX_APP_LARGEST_BLOCK,
      StrId::STR_YBX_APP_FREE_PSRAM, StrId::STR_YBX_APP_UPTIME,
  };
  for (int i = 0; i < kRows; ++i) {
    items_[i].label = I18n::getInstance().get(names[i]);
    items_[i].value = values_[i].data();
    items_[i].actionValue = static_cast<int16_t>(i);
  }
  refreshValues();
  UiListActivity::onEnter();
}

void MemoryStatusActivity::refreshValues() {
  snprintf(values_[0].data(), values_[0].size(), "%lu KiB", static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  snprintf(values_[1].data(), values_[1].size(), "%lu KiB",
           static_cast<unsigned long>(ESP.getMaxAllocHeap() / 1024));
  if (ESP.getPsramSize() > 0) {
    snprintf(values_[2].data(), values_[2].size(), "%lu KiB", static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  } else {
    snprintf(values_[2].data(), values_[2].size(), "%s", tr(STR_YBX_APP_NOT_AVAILABLE));
  }
  snprintf(values_[3].data(), values_[3].size(), "%lu min", static_cast<unsigned long>(millis() / 60000u));
}

const char* MemoryStatusActivity::headerTitle() const { return tr(STR_YBX_APP_MEMORY); }

void MemoryStatusActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const int16_t noteHeight = static_cast<int16_t>(2 * screen.target().lineHeight(screen.theme().smallText.font));
  screen.target().text(screen.takeTop(noteHeight, screen.theme().spaceMd), tr(STR_YBX_APP_REFRESH_HINT),
                       screen.theme().smallText);

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(kRows);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void MemoryStatusActivity::activateIndex(int index) {
  if (index < 0 || index >= kRows) return;
  {
    RenderLock lock(*this);
    refreshValues();
  }
  requestUpdate();
}
