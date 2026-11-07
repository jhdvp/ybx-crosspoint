#pragma once

#include <array>

#include "NotesStorage.h"
#include "activities/UiListActivity.h"

class NotesActivity final : public UiListActivity {
 public:
  NotesActivity(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("NativeNotes", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void onScreenCovered() override;
  void loop() override;

 private:
  static constexpr size_t kRows = nativeapps::notes::kMaxNotes + 1;
  int listCount() const override { return static_cast<int>(rowsUsed_); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { finish(); }
  const char* headerTitle() const override;

  void reload();
  void releaseRows();
  void startNewNote();
  bool needsReload_ = false;
  size_t rowsUsed_ = 1;
  nativeapps::notes::List notes_;
  std::array<freeink::ui::ListItem, kRows> rows_{};
};
