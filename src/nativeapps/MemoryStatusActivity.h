#pragma once

#include <array>

#include "activities/UiListActivity.h"

// Actual read-only device diagnostic. No SD writes, Wi-Fi, worker or tokens.
// Every activation refreshes values; leaving frees this activity's UI state.
class MemoryStatusActivity final : public UiListActivity {
 public:
  MemoryStatusActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("NativeMemoryStatus", renderer, input) {}
  void onEnter() override;

 private:
  static constexpr int kRows = 4;
  int listCount() const override { return kRows; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { finish(); }
  const char* headerTitle() const override;
  void refreshValues();

  std::array<freeink::ui::ListItem, kRows> items_{};
  std::array<std::array<char, 24>, kRows> values_{};
};
