#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/controls/button.h"
#include "util/ButtonNavigator.h"
#include "util/YbxAwardsLayout.h"
#include "util/YbxReadingData.h"

class YbxStatisticsActivity final : public Activity, public UiAppHost {
 public:
  YbxStatisticsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("YbxStatistics", renderer, input), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View { Statistics, Awards, AwardDetail };
  struct AwardBuffers {
    ybx::AwardsPage shown, working;
    ybx::AwardsLayout layout;
  };
  std::unique_ptr<AwardBuffers> awards;
  View view = View::Statistics;
  int detailIndex = 0;
  enum class State { Cached, Loading, Updated, Failed };
  static void screen(UiScreen& screen, void* user);
  static void action(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void refresh();
  void openAwards();
  void back();
  void activate(int action);
  void loadAwards(ybx::AwardSection section, size_t offset);
  void drawAwards(UiScreen& screen);
  void drawAwardDetail(UiScreen& screen);
  void drawButton(UiScreen& screen, freeink::ui::Rect bounds, int action, const char* label, bool enabled = true,
                  bool tabSelected = false);
  const char* statusMessage() const;
  const char* datedStatus(const char* message, int64_t epoch, const char* caption = nullptr);
  ybx::ReadingSummary summary;
  ybx::ReadingSummary working;
  freeink::ui::ButtonProps button;
  ButtonNavigator navigator;
  std::array<char, 96> finishedText{};
  std::array<char, 160> statusText{};
  State state = State::Cached;
  ybx::ReadingResult result = ybx::ReadingResult::Ok;
  int selection = 1;
  int pending = -1;
  bool refreshPending = false;
  bool ownsWifi = false;
  bool childReturnFrame = false;
  bool waitConfirmRelease = false;
  bool waitBackRelease = false;
};
