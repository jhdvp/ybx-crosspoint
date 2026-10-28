#pragma once

#include <array>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/controls/button.h"
#include "util/ButtonNavigator.h"
#include "util/YbxAccountModel.h"

// Read-only native account sources. The plugin catalog remains the sole
// device-code/pairing owner: this screen never mints, persists or deletes keys.
class YbxAccountActivity final : public Activity, public UiAppHost {
 public:
  YbxAccountActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("YbxAccount", renderer, input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { NotConnected, Ready, Loading, Failed, InvalidToken, Unsupported };
  static void screen(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void button(UiScreen& screen, freeink::ui::Rect bounds, int value, const char* label);
  void refresh();
  void activate(int value);
  void openPluginCatalog();

  ybxaccount::Summary sources_{};
  State state_ = State::NotConnected;
  bool fetched_ = false;
  bool ownsWifi_ = false;
  bool refreshPending_ = false;
  bool returnedFromChild_ = false;
  bool waitConfirmRelease_ = false;
  bool waitBackRelease_ = false;
  int selection_ = 1;
  int pending_ = -1;
  ButtonNavigator navigator_;
  freeink::ui::ButtonProps props_;
  std::array<std::array<char, 96>, 3> rows_{};
};