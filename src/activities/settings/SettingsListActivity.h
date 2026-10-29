#pragma once

#include <array>

#include "SettingsSection.h"
#include "util/YbxSettingsNavigation.h"
#include <string>
#include "activities/UiListActivity.h"

// Settings root: one row per SettingsSection, each opening a
// SettingsActivity with that group's settings.
class SettingsListActivity final : public UiListActivity {
  // Section opened on the first loop pass (End = none), e.g. returning to
  // Network after the Wi-Fi rows' silent restart.
  SettingsSection pendingSection;
  bool ybxDashboard_ = false;
  bool ybxRootEntry_ = false;
  std::array<freeink::ui::ListItem, SETTINGS_SECTION_COUNT> rowItems_{};
  std::array<freeink::ui::ListItem, ybxsettings::ORDER.size()> ybxRows_{};
  std::array<std::string, ybxsettings::ORDER.size()> ybxValues_{};

  void rebuildRows();
  void openSection(SettingsSection section);
  void openYbxRow(int index);
  void populateYbxValues();
  void showSystemSettings();

  int listCount() const override {
    return ybxDashboard_ ? static_cast<int>(ybxsettings::ORDER.size())
                         : static_cast<int>(SETTINGS_SECTION_COUNT);
  }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void drawChrome() override;

 public:
  explicit SettingsListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                SettingsSection openSection = SettingsSection::End);
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
