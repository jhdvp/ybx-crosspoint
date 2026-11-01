#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

namespace {
namespace fui = freeink::ui;
constexpr fui::ActionId ACTION_RETRY = 1;
constexpr fui::ActionId ACTION_BACK = 2;
}  // namespace

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  runUpdateCheck();
}

// Lookup + confirmation. Retry on the FAILED screen re-enters this while the
// Wi-Fi connection this activity opened is still up.
void OtaUpdateActivity::runUpdateCheck() {
  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    failureStage = FAILURE_CHECK;
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate();
  // NO_UPDATE here means the release carries no firmware asset for this board
  // (expected until per-board assets are published) — not a failure.
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
      // The first check runs from WifiSelectionActivity's result handler, where
      // ActivityManager repaints after the pop; a retried check runs from
      // loop() and must request its own frame.
      requestUpdate();
    }
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    enterFailed(res == OtaUpdater::OOM_ERROR ? tr(STR_MEMORY_ERROR) : tr(STR_UPDATE_NETWORK_HINT), true);
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
      requestUpdate();
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  // Default the selection to Update so the hardware Confirm button installs,
  // matching the pre-popup layout (Back = cancel, Confirm = update).
  confirmPopup.show(tr(STR_NEW_UPDATE), options, 2, 1, [this](const int idx) {
    if (idx == 1) {
      runUpdateInstall();
    } else {
      finish();
    }
  });
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  resetUi();
  app.on(ACTION_RETRY, &OtaUpdateActivity::onRetryEvent, this);
  app.on(ACTION_BACK, &OtaUpdateActivity::onBackEvent, this);
  app.setScreen(&OtaUpdateActivity::failedScreen, this);

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    // Version info sits in the upper part of the screen so the centered
    // Cancel/Update popup doesn't cover it (same layout as ConfirmationActivity).
    const int infoTop = pageHeight / 6;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop,
                      (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop + height + metrics.verticalSpacing,
                      (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str());

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_PREPARING) {
    // The slot erase runs before the first byte; a 0 % bar here would look
    // like a hang for tens of seconds. No percentage until data arrives.
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_PREPARING), true, EpdFontFamily::BOLD);
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    // Percent label is drawn by BaseTheme::drawProgressBar; this slot is left intentionally empty
    // so the bytes line below stays at the same Y it was at when the activity drew its own percent.
    y += height + metrics.verticalSpacing;
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    // Labeled Retry/Back controls, also on touch boards, which draw no hint
    // bar. Their hit rects are registered here on the render task.
    renderUi();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), allowRetry ? tr(STR_RETRY) : "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    const int hintY = top + height + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_POWER_ON_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    failureStage = FAILURE_INSTALL;
    failedDetail = nullptr;
    // Force the first progress frame to paint: render() skips the refresh when
    // the percent looks unchanged, and a retried install starts at 0 again.
    lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
    state = UPDATE_PREPARING;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        auto* self = static_cast<OtaUpdateActivity*>(ctx);
        {
          RenderLock lock(*self);
          // installUpdate() reports once right after the slot erase, before the
          // first bytes arrive. From here on the progress bar shows real bytes.
          if (self->state == UPDATE_PREPARING) {
            self->lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
            self->state = UPDATE_IN_PROGRESS;
          }
        }
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        self->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    const char* detail = nullptr;
    if (res == OtaUpdater::WRONG_DEVICE_ERROR) {
      detail = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else if (res == OtaUpdater::HTTP_ERROR) {
      detail = tr(STR_UPDATE_NETWORK_HINT);
    }
    // A wrong-device image fails the same way on every attempt; a truncated
    // download or a failed flash write is worth re-running.
    enterFailed(detail, res != OtaUpdater::WRONG_DEVICE_ERROR);
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::failedScreen(UiScreen& screen, void* user) {
  const auto* self = static_cast<const OtaUpdateActivity*>(user);
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Same band as the Wi-Fi list: below the header, clear of the hint bar that
  // buttons-only profiles still draw.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(self->renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      static_cast<int16_t>(self->renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(self->renderer.getScreenHeight() - (safe.y + safe.height)),
      static_cast<int16_t>(safe.x)});

  const auto& theme = screen.theme();
  const fui::Rect body = screen.body();
  const fui::Rect band{body.x, static_cast<int16_t>(body.y + body.height * 2 / 3), body.width, theme.rowHeight};

  const auto drawButton = [&screen, &theme](const char* label, const fui::ActionId action, const fui::Rect rect) {
    fui::ButtonProps props;
    props.label = label;
    props.action = action;
    props.inputMask = fui::InputTouch;
    props.text = theme.bodyText;
    // Outlined like the OPDS publication page's action, so the control reads as
    // a button on a screen that also carries plain text.
    props.styles = theme.button;
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = 1;
    props.radius = theme.controlRadius;
    fui::button(screen.frame(), rect, props);
  };

  // Back left, Retry right: the same order as the physical Back/Retry hints on
  // buttons-only profiles. A wrong-device image repeats identically, so that
  // failure offers Back alone.
  if (self->allowRetry) {
    const int16_t gap = theme.spaceMd;
    const int16_t width = static_cast<int16_t>((band.width - gap) / 2);
    drawButton(tr(STR_BACK), ACTION_BACK, fui::Rect{band.x, band.y, width, band.height});
    drawButton(tr(STR_RETRY), ACTION_RETRY,
               fui::Rect{static_cast<int16_t>(band.x + width + gap), band.y,
                         static_cast<int16_t>(band.width - width - gap), band.height});
  } else {
    const int16_t width = static_cast<int16_t>(band.width / 2);
    drawButton(tr(STR_BACK), ACTION_BACK,
               fui::Rect{static_cast<int16_t>(band.x + (band.width - width) / 2), band.y, width, band.height});
  }
}

void OtaUpdateActivity::onRetryEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OtaUpdateActivity*>(user);
  self->app.clearTapFlash();
  // The last paint may still show Retry while enterFailed() has already
  // withdrawn it; the flag, not the stale hit table, decides.
  if (self->allowRetry) {
    self->retryFailedStep();
  }
}

void OtaUpdateActivity::onBackEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OtaUpdateActivity*>(user);
  self->app.clearTapFlash();
  finish();
}

void OtaUpdateActivity::enterFailed(const char* detail, const bool canRetry) {
  {
    RenderLock lock(*this);
    failedDetail = detail;
    allowRetry = canRetry;
    state = FAILED;
  }
  requestUpdate();
}

void OtaUpdateActivity::retryFailedStep() {
  if (failureStage == FAILURE_INSTALL) {
    runUpdateInstall();
  } else {
    runUpdateCheck();
  }
}

void OtaUpdateActivity::loop() {
  if (state == WAITING_CONFIRMATION) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // Popup dismissed without a selection (Back button or tap outside): cancel.
    finish();
    return;
  }

  if (state == FAILED) {
    // Taps route only to the labeled Back/Retry controls; anywhere else does
    // nothing, so an unlabeled tap can never start a slot-erasing re-flash.
    const auto route = routeTouch(mappedInput);
    if (route.routed && app.invalidated()) requestUpdate();
    if (route) return;
    // Physical keys keep working on every profile. The header chevron on touch
    // profiles folds into Back (HeaderBackTapTarget), like every other screen.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (allowRetry && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      retryFailedStep();
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
