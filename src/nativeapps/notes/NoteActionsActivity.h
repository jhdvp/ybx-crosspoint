#pragma once

#include <array>
#include <string>

#include "NotesStorage.h"
#include "activities/UiListActivity.h"

class NoteActionsActivity final : public UiListActivity {
 public:
  NoteActionsActivity(GfxRenderer& renderer, MappedInputManager& input, std::string fileName)
      : UiListActivity("NativeNoteActions", renderer, input), fileName_(std::move(fileName)) {}
  void onEnter() override;

 private:
  int listCount() const override { return loaded_ ? 4 : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { finish(); }
  const char* headerTitle() const override { return tr(STR_YBX_NOTES); }

  void startEdit(bool title);
  void confirmDelete();

  std::string fileName_;
  nativeapps::notes::Document doc_;
  bool loaded_ = false;
  std::array<freeink::ui::ListItem, 4> rows_{};
};
