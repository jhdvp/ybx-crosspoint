#pragma once

#include <array>

#include "activities/UiListActivity.h"
#include "CatalogPolicy.h"

// FreeInkUI launcher, integrated with the existing YBX Home -> More menu.
// PluginHub remains an independent declarative catalog/install flow.
class NativeAppsActivity final : public UiListActivity {
 public:
  NativeAppsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("NativeApps", renderer, input) {}

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(count_); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

  size_t count_ = 0;
  std::array<freeink::ui::ListItem, nativeapps::catalog::kMaxApps> items_{};
};
