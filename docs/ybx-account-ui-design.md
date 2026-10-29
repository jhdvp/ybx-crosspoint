# YBX Reader — canonical Native Account UI implementation

This review branch is deliberately based on the **existing approved visual direction** rather than a new mockup. The canonical source files remain at `~/Projects/YBX/artifacts/ybx-screen-plans-20261028/` (gallery `index.html`, generator `draw_screens.py`). Three matching reference PNGs are copied into this directory solely for review:

| 06 — Источники | 07 — Подключение | 08 — Настройки |
|---|---|---|
| ![Sources](images/ybx-reader-20261028/06-sources.png) | ![Pairing](images/ybx-reader-20261028/07-pair.png) | ![Settings](images/ybx-reader-20261028/08-settings.png) |

**Important:** these PNGs were generated on 28 October, use illustrative account data, are **not firmware screenshots**, and their sample QR is **not linked to a device**. The firmware uses real data and the real verification URI.

## Implemented screen treatment

The native source list (`YbxAccountActivity`) now follows the 06-sources reference:
- YBX wordmark + three-stroke mark, separator, large separate screen title, subtitle.
- E-ink grayscale paper, dark text, thick source borders, inverted letter badges for catalog-listed services (Я / WB / Л), outline badges otherwise.
- Each source card shows actual catalog membership, with a lower action band in portrait mode. Narrow/landscape mode collapses to the compact badge/name/status row. The 3 bottom actions remain for touch users and hardware focus.
- The **Update all** action refreshes the device-wide catalog membership; the **Manage sources** action opens the verified `https://my.ybx.app/cabinet#sources` QR for cabinet-authenticated modifications. **The firmware cannot unlink a provider using a reader token**. The reference screenshot's direct `Отключить` therefore is not implemented as a misleading button.
- Error, empty, unpaired and expired-token states remain backed by existing data, never demo data. Listed does not assert a healthy upstream provider OAuth session.

Pairing follows 07-pair:
- Large real QR above the one-time code (portrait); in landscape, the QR and code are side by side. QR is white with a quiet-zone margin even on a gray e-ink screen.
- Code label, prominently rendered code, explanation, bounded minutes-remaining indicator and explicit **New code**, **Cancel** and full-screen QR interaction.
- The QR is tappable on touch models; on hardware-only models it is accessible by navigation/focus. Code renewal preserves the user's explicit account-switch confirmation and does not silently overwrite the existing token.
- The existing protected token storage, cancellation, TLS checks and rate-limited polling logic remain unchanged.

Settings / 08-settings:
- `SettingsActivity` / General now exposes a direct **YBX Account** row, in addition to the existing YBX Home → More entry. The screen uses `finish()` when exiting so the parent Settings activity is restored correctly.
- Other firmware Settings sections retain their existing behavior. This change **does not claim** to be a complete rewrite of the screenshot's eight-row settings design.

## Original screen geometry

`src/util/YbxAccountLayout.h` derives panels from the rotated safe area, scales card heights and QR size, preserves 48px action hit targets, and switches to compact two-column pairing in landscape. The layout is tested for both 480×800 and 528×792 with all four rotations and multiple line-height profiles. The implementation uses real FreeInk text, buttons, touchscreen hit targets, theme-safe hardware footer labels and actual firmware header status.

The monochrome firmware uses FreeInk's nearest e-ink gray (`Color::LightGray`) instead of assuming an exact RGB match to the PNG's `#e7e8e3`. Whether the panel physically renders that tone faithfully is an **on-device visual acceptance test**, not a host build assertion.

## Open verification gates

- Hardware screenshots of sources and pairing on X3, X4 and X4 Pro in portrait/landscape must be compared with the original PNGs; check contrast, text size, long locales, real QR scan/quiet zone, and touch/button focus.
- Native FreeInk settings page does not yet fully match the eight-row 08-settings concept: evaluate its separate redesign with system settings owners before changing its information architecture.
- UI-only validation cannot prove backend state/QR scanability on a device, account switching under SD corruption or provider OAuth session health.
- No firmware flashing, main merge, OTA publication or production deployment without explicit acceptance.

This branch is intentionally review-only, stacked on the native-account implementation PR.
