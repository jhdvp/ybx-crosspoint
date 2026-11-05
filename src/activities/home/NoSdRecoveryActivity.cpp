#include "NoSdRecoveryActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include "fontIds.h"

void NoSdRecoveryActivity::onEnter() {
  Activity::onEnter();
  requestUpdateAndWait();
}

void NoSdRecoveryActivity::loop() {
  if (rebooting) return;
  int x = 0;
  int y = 0;
  const bool requested = mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                         (mappedInput.hasTouch() && mappedInput.wasScreenTapped(x, y));
  if (!retryGate.consume(millis(), requested)) return;

  if (Storage.begin()) {
    // No settings, files, networking or event queues have been initialized.
    // A full restart is required to enter the standard boot path in order.
    rebooting = true;
    LOG_INF("SD", "Recovery mount succeeded, restarting normal boot");
    ESP.restart();
    return;
  }

  attempted.store(true);
  LOG_ERR("SD", "Recovery mount failed; SD may still be missing or unreadable");
  requestUpdate();
}

void NoSdRecoveryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int height = renderer.getScreenHeight();
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int block = titleLine + line * 5;
  int y = (height - block) / 2;

  renderer.drawCenteredText(UI_12_FONT_ID, y, "SD CARD UNAVAILABLE", true, EpdFontFamily::BOLD);
  y += titleLine + line;
  renderer.drawCenteredText(UI_10_FONT_ID, y, "Insert a readable SD card");
  y += line;
  renderer.drawCenteredText(UI_10_FONT_ID, y, "Press OK / Right to retry");
  y += line;
  if (mappedInput.hasTouch()) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Or tap the screen");
  }
  y += line;
  renderer.drawCenteredText(UI_10_FONT_ID, y,
                            attempted.load() ? "Card still unavailable" : "Files will not be erased");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
