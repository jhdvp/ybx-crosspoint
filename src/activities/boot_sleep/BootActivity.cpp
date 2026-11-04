#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // Draw through logical pixels: drawImage blits unrotated bits on rotated panels.
  constexpr int kLogoSize = 120;
  const int left = (pageWidth - kLogoSize) / 2;
  const int top = (pageHeight - kLogoSize) / 2;
  for (int y = 0; y < kLogoSize; ++y) {
    for (int x = 0; x < kLogoSize; ++x) {
      const int index = y * kLogoSize + x;
      if ((Logo120[index / 8] & (0x80u >> (index % 8))) == 0) renderer.drawPixel(left + x, top + y, true);
    }
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_YBX), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
