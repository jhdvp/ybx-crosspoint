# YBX Reader P2-H · native apps framework, stage 1

**Kaneo:** YBX Reader / firmware #55
**Stack base:** perf/ybx-reader-progress-write-gate-20261103 (draft PR #12)
**Upstream reference:** InkHome (mohitagw15856/inkhome) commit 735e74d73b95,
files src/apps/AppRegistry.h and src/apps/AppRegistry.cpp; MIT.
This change independently implements the descriptor/factory concept against
the existing YBX FreeInkUI activity model; no InkHome source was imported.

## User-visible result

YBX Home -> More -> Apps -> Memory status, on X3/X4/X4 Pro only.
The Apps row is absent when the compiled registry has no available modules.
Existing More actions (On Reader, Browse files, File transfer, Settings,
My Account) preserve their relative positions. PluginHub catalog entries
and provider authorization remain independent of native apps.

The launcher is a FreeInkUI list with the same physical button navigation
and X4 Pro touch routing as the existing Settings list. Opening a native
application creates its Activity on demand. Leaving returns to the launcher,
then Home. An allocation failure reports a localized memory error.

The first real bundled application is Memory status, a read-only diagnostics
panel with manually refreshed free heap, largest contiguous heap block,
available PSRAM or Not available, and uptime. Press a row to refresh. No
Wi-Fi startup, recurring polling, SD writes, tokens, background worker,
personal device identifier, or data export occurs.

## Adding a new compiled native app

1. Write a bounded Activity in src/nativeapps or src/nativeapps/<id>. Verify
   that it can enter and leave without retaining UI buffers, workers, radio
   state or hidden SD handles.
2. Add a factory and a unique lowercase/digit/dash ID to AppRegistry.cpp.
   Register StrId name and short summary; add strings to the English
   translation, with locale-specific translations as available.
3. Use a compile-time device/feature gate. Every descriptor carries a
   minimum contiguous free-heap requirement, tested before allocation.
4. For app-owned SD data, build paths with AppPaths.h. Read-only content
   convention: /apps/<id>. App state convention:
   /.crosspoint/apps/<id>. The helper rejects invalid IDs and buffers too
   small to hold the complete directory. The first Memory status app has
   no file state or content.
5. Add host tests for core state/model/paths and per-board PlatformIO builds.
   Capture largest free block, flash delta, stack use, and hardware
   enter/back/rotation/touch before release.

The path contract is a **convention**, not a filesystem sandbox; compiled
C++ code can use the platform HAL. Every new module needs a code/security
review. PluginHub packages remain declarative integrations, not installable
executable apps. Never run code loaded from SD or grant remote sources
arbitrary local execution.

## Compile/flash constraints

Only the Memory status module is linked in this first iteration. No heavy
InkHome notes/flashcards/Wiki engines or fork-wide assets were copied.
The registry is an allocation-free compile-time table, limited to 12 entries.
The launcher retains only a bounded array of 12 list rows, and activities
are allocated when entered.

The X3/X4 ESP32-C3 profile has no PSRAM. The X4 Pro ESP32-S3 profile has
PSRAM but cannot use it as justification for regressions on C3. All compiled
module sizes must remain within the actual board partition and measured
heap/stack budgets. Enabling other apps is a separate feature PR.

Language support: English, Russian and Belarusian have native strings for
this stage. Other enabled firmware locales fall back to English for the
new keys. Existing language selection logic is unchanged.

## Verification and open acceptance gates

- Pure C++ test suite covers empty registry, malformed and duplicate IDs,
  missing factories, supported-device filtering, stable order, out-of-range
  access, 12-app capacity, safe paths and traversal rejection.
- Host test suite and both ESP32-C3 / ESP32-S3 builds must pass at the
  final head. Report results only after successful verification.
- Physical X3/X4/X4 Pro interactive acceptance is OPEN: check More menu
  with/without downloaded shelf; Settings and account still work; Home ->
  Apps -> Memory -> Back -> Apps -> Home; Confirm and touch; free-heap delta
  after 100 open/close cycles; language labels and rotations; disabled app;
  insufficient contiguous heap and reset/reboot.
- QEMU screen capture/interaction requires a working emulator EPD/input
  profile, so a compiler pass is not accepted as proof of rendered visuals.
- No flash to any real device, OTA, release or production deployment.

## Verified host and board build evidence (2026-11-03)

- Host CMake/CTest: 1135/1135 PASS (18.66 s), including 10 new
  NativeAppsCatalog tests covering metadata, device filtering, safe paths
  and reject cases.
- ESP32-C3 default (X3/X4) PlatformIO: PASS.
  Static RAM: 66,904 / 327,680 bytes (20.4%).
  Application flash: 5,687,037 / 6,553,600 bytes (86.8%).
  Retained firmware.bin: 5,701,568 bytes.
  SHA-256 6ceacc762b41631bb2cd1c213a7feec6f25b8a08d2ac75072cbbd3a391bbffae.
- ESP32-S3 x4pro PlatformIO: PASS.
  Static RAM: 111,956 / 327,680 bytes (34.2%).
  Application flash: 5,529,230 / 6,553,600 bytes (84.4%).
  Retained firmware.bin: 5,534,496 bytes.
  SHA-256 b3436405650d831d817d03c7c0bd4258ecd7f64db713c8eaf91f9e4c2f497d4c.
- Relative to the immediately preceding firmware change (PR #12):
  +7,112 bytes C3 flash and +5,764 bytes S3 flash, with no static RAM delta.
  This is a code/asset build comparison, not a free-heap or battery result.
- Both binaries and full raw logs retained locally under
  ~/Projects/Ybx/artifacts/ybx-native-apps-20261103.
  They are not release assets; no OTA or device flashing was performed.

## Remaining release blockers

- 100-cycle launcher / diagnostics / back test on physical boards, measured
  max contiguous heap and stack headroom before and after.
- Actual X4 Pro touch navigation and X3/X4 button behavior, including More
  with/without On Reader shelf, locale strings and all supported rotations.
- Energy, sleep/restart, low-memory and card-removal tests.
- Do not merge this draft PR chain to main based on build evidence alone.
