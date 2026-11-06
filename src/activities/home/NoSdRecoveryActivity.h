#pragma once

#include <atomic>

#include "activities/Activity.h"
#include "util/NoSdRecoveryPolicy.h"

// SD-independent recovery. The normal app/settings/plugins must not run until
// Storage.begin() succeeds and the entire setup sequence has restarted.
class NoSdRecoveryActivity final : public Activity {
  nosd::RetryGate retryGate;
  std::atomic<bool> attempted{false};
  bool rebooting = false;

 public:
  NoSdRecoveryActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("NoSdRecovery", renderer, input) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool handleHomeGesture() override { return true; }
};
