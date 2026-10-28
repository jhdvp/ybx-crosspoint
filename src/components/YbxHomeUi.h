#pragma once

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "components/controls/button.h"
#include "components/media/book-card.h"
#include "util/YbxHomeLayout.h"

// Local-only home. No service credentials, network jobs or second framebuffer.
class YbxHomeUi final : public UiAppHost {
 public:
  explicit YbxHomeUi(GfxRenderer& renderer);
  void begin(const std::vector<RecentBook>& books, bool catalogs);
  void setSelection(int value) { selected = value; }
  int selectedAction(const MappedInputManager& input);
  void refreshCoverPaths();
  int thumbHeightFor(size_t index) const;
  bool takeThumbHeightChanged();

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void drawHero(UiScreen& screen, freeink::ui::Rect bounds);
  void drawRecent(UiScreen& screen, freeink::ui::Rect bounds);
  void drawCover(freeink::ui::Rect bounds, size_t index);
  void drawAction(UiScreen& screen, freeink::ui::Rect bounds, int value, const char* label, bool compact = false);

  GfxRenderer& renderer;
  HomeCoverCache covers;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, ybxhome::MAX_BOOKS> coverPaths;
  std::array<int, ybxhome::MAX_BOOKS> thumbHeights{};
  std::array<int, ybxhome::MAX_BOOKS> progress{};
  std::array<std::array<char, 12>, ybxhome::MAX_BOOKS> progressText{};
  freeink::ui::BookCardProps card;
  freeink::ui::ButtonProps button;
  bool catalogs = false;
  bool thumbsChanged = false;
  int selected = 0;
  int pending = -1;
};
