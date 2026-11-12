# YBX Reader — FreeInkUI Native Simulator (host)

This is a **working native C++ framebuffer renderer**, **not** an ESP32 CPU emulator.
It compiles the actual FreeInkUI `FreeInkUI.cpp`, uses the real `DisplayTarget`
1-bit renderer, the reader's shared `YbxAccountLayout.h`, and
`YbxSettingsNavigation.h` to generate native frames. A browser viewer
shows those frames at logical X3/X4/X4 Pro sizes with clickable hit rectangles.

It **does not** execute `firmware.bin`, `YbxAccountActivity`/Arduino's event
loop, the actual SPI display driver, Wi-Fi stack, SD card, TLS, or production
YBX APIs. The host view reproduces the account screens using shared layout
and FreeInkUI primitives. Text comes from safe demo constants, not an account.

## Quick start on Mac

```sh
cd tools/ybx-native-sim
sh build.sh
python3 serve.py
# open http://127.0.0.1:8768/
```

Requires Apple's `clang++`, Python 3 and Pillow, and the
**locally installed** `/System/Library/Fonts/Supplemental/Arial Unicode.ttf`.
Font rasters and compiled binary are generated in
`/tmp/ybx-native-freeink-sim/`; the font or compiled font data is not committed
or distributed.

For browsing from another device on your trusted LAN, explicitly use
`python3 serve.py --host <your-Mac-LAN-IP> --port 8768`.
The server has no authentication, **do not port-forward or publish it**.

## Screens / interactions

- **Источники:** two/three/zero sources, expired reader token, revoked
  device, offline, provider-session issue and refresh state.
- **Подключение:** native QR placeholder and device-code/status geometry.
  The placeholder is **intentionally not a scannable QR** and carries no token.
- **Настройки:** nine genuine YBX settings destinations, rendered with native
  `FreeInkUI::settingRow`.

Model selector: X3, X4 (480×800) and X4 Pro (528×792).
Orientation selector: four physical render rotations, converted back from the
panel-native 1bpp framebuffer to logical upright frames by the viewer.
Click the actual on-screen hit regions or use ↑/↓/Enter/Esc.

The original October 28 PNGs are shown beside the live output as visual
references. They have illustrative data, not captured device pixels.

## Internal contract

`build.sh` builds three Cyrillic bitmap fonts from the Mac's local system
TTF through FreeInk's own `gen_font.py`, compiles the local SDK's
`src/FreeInkUI.cpp`, the `renderer.cpp` and shared reader model headers.
`renderer.cpp` writes a panel-native PGM and a JSON action-hit map.
`serve.py` converts PGM to PNG using Pillow, rotates it to logical UI
coordinates and returns an in-memory JSON response through `/frame`.
It accepts **only enumerated values**, never accesses credentials and invokes
the render binary with a fixed argv list and no shell.

```sh
YBX_SIM_URL=http://127.0.0.1:8768 python3 smoke.py
```

The smoke test verifies 3 models × 3 screens × 4 rotations, pixel invariance
under inverted orientation, 1-bit content and interactive coordinate bounds,
plus ten source states (**46 PNG frames**). Browser interactions can be
exercised through Playwright/Chrome (optional, external to the build).

## Known differences / next phase

1. The host renderer **re-implements** the UI surface in a separate host
   adapter but uses shared geometry and actual FreeInkUI drawing/row primitives.
   To claim pixel identity with firmware, move pure screen painting into a
   reusable library compiled in both host and ESP paths.
2. The live QR is intentionally absent; verifying real device-code QR,
   TLS and physical scan requires firmware and hardware or a much fuller
   peripheral emulator.
3. Different host bitmap font metrics and 1bpp quantization mean screenshots
   are useful regression artifacts, not guaranteed identical to Xteink hardware.
4. On shorter landscape screens, a real settings viewport/focus-scroll
   implementation should be exercised on the reader.
5. No actual firmware flashing or production deployment is performed.
