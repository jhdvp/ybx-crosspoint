#pragma once

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public UiListActivity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 12;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  // YBX rows lead the list while the reader carries a pairing: the opt-out switch,
  // the sync status, and (when a hand-typed server is in the way) the consent to
  // replace it. The prefix length follows the pairing, so the manual rows' slots
  // and action values move with it; stampRows() re-stamps them only when it changes.
  static constexpr int YBX_MAX_ROWS = 3;
  static constexpr int ROW_CAPACITY = YBX_MAX_ROWS + MENU_ITEMS;

  int ybxRowCount() const;
  void stampRows(int ybxRows);
  // Value text of the status row: verdict plus the age of the last transaction.
  const std::string& ybxStatusText();

  OptionPopup optionPopup;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  // Row storage: the count is bounded by ROW_CAPACITY, so fixed-capacity storage
  // avoids any heap allocation for the row list. Labels are plain pointers into
  // the translation tables; buildScreen() refreshes only the live value text
  // (rowValues_) by assigning into the existing strings (no array growth).
  std::string rowValues_[MENU_ITEMS];
  std::string ybxStatusValue_;
  freeink::ui::ListItem rowItems_[ROW_CAPACITY]{};
  int stampedYbxRows = -1;
};
