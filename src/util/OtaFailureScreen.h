#pragma once

#include <FreeInkUICore.h>

#include <algorithm>

#include "network/OtaUpdater.h"

// The pure decisions behind the OTA update failure screen: what the failure
// line says, whether re-running the failed step can help, and where the two
// on-screen controls sit. Kept out of the activity so the host suite covers
// the tables without a panel.
namespace otafailure {

enum class Reason {
  Memory,       // an allocation for the release lookup failed
  Network,      // no HTTP answer, or the body stopped early
  Server,       // the release JSON arrived but could not be read
  WrongDevice,  // the image names another board or MCU
  NoUpdate,     // the release is not newer than the running firmware
  FlashWrite,   // the OTA partition APIs refused the write
  Generic,      // anything else
};

struct Failure {
  Reason reason;
  // False when repeating the step returns the same answer, so the screen
  // offers Back alone.
  bool allowRetry;
};

// `error`: the non-OK value a step returned. OK/NO_UPDATE are not failures;
// they land on a retry-less Generic so a caller that routes one here by
// mistake cannot loop the user on a screen that re-runs a step that already
// succeeded.
constexpr Failure classify(const OtaUpdater::OtaUpdaterError error) {
  switch (error) {
    case OtaUpdater::OOM_ERROR:
      return {Reason::Memory, true};
    case OtaUpdater::HTTP_ERROR:
      return {Reason::Network, true};
    case OtaUpdater::JSON_PARSE_ERROR:
      return {Reason::Server, true};
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return {Reason::WrongDevice, false};
    case OtaUpdater::UPDATE_OLDER_ERROR:
      return {Reason::NoUpdate, false};
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return {Reason::FlashWrite, true};
    case OtaUpdater::OK:
    case OtaUpdater::NO_UPDATE:
      break;
  }
  return {Reason::Generic, false};
}

struct Buttons {
  freeink::ui::Rect back{};
  freeink::ui::Rect retry{};
  bool hasRetry = false;
  // False when the band holds no control at all: the caller then draws none
  // rather than a rect that leaves the content area.
  bool valid = false;
};

// Places Back — and Retry beside it when `allowRetry` — in the lower third of
// `body`, sized to the room that band actually has. `body` shrinks with the
// panel orientation, the header and the hint bar, so a themed row height can
// reach past a short band and a themed gap can leave a narrow band with
// negative control widths; both rects stay inside the band or the pair is
// dropped (`valid` false).
inline Buttons layout(const freeink::ui::Rect body, const int gap, const int rowHeight, const bool allowRetry) {
  Buttons buttons;
  const int bandTop = static_cast<int>(body.y) + static_cast<int>(body.height) * 2 / 3;
  const int height = std::min(rowHeight, static_cast<int>(body.y) + static_cast<int>(body.height) - bandTop);
  if (body.width <= 0 || height <= 0) {
    return buttons;
  }

  const int width = static_cast<int>(body.width);
  const int16_t x = freeink::ui::clampI16(body.x);
  const int16_t y = freeink::ui::clampI16(bandTop);
  const int16_t h = freeink::ui::clampI16(height);
  if (allowRetry) {
    // A gap wider than a third of the band would leave nothing for the controls.
    const int usableGap = std::clamp(gap, 0, width / 3);
    const int each = (width - usableGap) / 2;
    if (each <= 0) {
      return buttons;
    }
    buttons.valid = true;
    buttons.hasRetry = true;
    buttons.back = freeink::ui::Rect{x, y, freeink::ui::clampI16(each), h};
    buttons.retry = freeink::ui::Rect{freeink::ui::clampI16(static_cast<int>(body.x) + each + usableGap), y,
                                      freeink::ui::clampI16(width - usableGap - each), h};
    return buttons;
  }

  const int single = std::max(1, width / 2);
  buttons.valid = true;
  buttons.back = freeink::ui::Rect{freeink::ui::clampI16(static_cast<int>(body.x) + (width - single) / 2), y,
                                   freeink::ui::clampI16(single), h};
  return buttons;
}

}  // namespace otafailure
