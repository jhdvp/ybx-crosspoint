#pragma once

#include <I18n.h>

#include <array>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/controls/button.h"
#include "util/ButtonNavigator.h"
#include "util/YbxAccountModel.h"

// First-party YBX Reader account: native device-code QR sign-in and connected
// catalog summary. An optional old plugin is used only as a token migration
// source, never as a prerequisite for authorization.
class YbxAccountActivity final : public Activity, public UiAppHost {
 public:
  YbxAccountActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("YbxAccount", renderer, input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Pairing || state_ == State::PairingStarting; }

 private:
  enum class State {
    NotConnected,
    Ready,
    Loading,
    Failed,
    InvalidToken,
    Unsupported,
    PairingStarting,
    Pairing,
    PairingFailed
  };
  static void screen(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void button(UiScreen& screen, freeink::ui::Rect bounds, int value, const char* label);
  void refresh();
  void beginPairing();
  void pollPairing();
  void activate(int value);
  void openPluginCatalog();

  ybxaccount::Summary sources_{};
  State state_ = State::NotConnected;
  bool fetched_ = false;
  bool ownsWifi_ = false;
  bool refreshPending_ = false;
  bool pairPending_ = false;
  uint32_t authPollIntervalMs_ = 0;
  uint32_t authNextPollMs_ = 0;
  uint32_t authDeadlineMs_ = 0;
  std::string authUserCode_, authVerifyUrl_, authDeviceCode_;
  const char* transportFailureText_ = nullptr;  // I18N flash-resident pointer, never a response body
  freeink::ui::Rect authQrRect_{};
  bool returnedFromChild_ = false;
  int selection_ = 1;
  int pending_ = -1;
  ButtonNavigator navigator_;
  freeink::ui::ButtonProps props_;
  std::array<std::array<char, 96>, 3> rows_{};
  std::array<char, 48> serverLabel_{};
  // Cached on the main task so draw() never reads the persisted server choice.
  StrId serverMode_ = StrId::STR_YBX_SERVER_AUTO;
};
