# YBX implementation status — 28 October 2026

This branch saves ongoing implementation work. Firmware handoff is incomplete;
these checks do not establish release readiness or hardware support acceptance.
The maintainer explicitly requested committing and pushing the current state
with the outstanding limitations already reported. This does not record design
acceptance or waive the remaining release checks.

## Implemented

- Compact X3/X4 Home using the existing UI host, local recent books, saved
  progress and a Library / Catalogs / More row. No account statistics are invented.
- Opt-in latest sleep GET delivery reserves an attempt after ordinary FIFO
  events, even when an earlier event fails. Older sleep requests are removed
  only after the latest succeeds.
- Queue replacement uses a complete pending file and a recoverable backup.
  Append repairs a bounded incomplete trailing event before writing another.
- Sleep drains use a cooperative time budget; authentication and retry share
  the event deadline. Streamed downloads require successful file close.

## Verification

- Host suite: **897/897 passed**, including 22 queue, storage and deadline cases.
  Storage tests cover short writes, failed renames/rollback, interrupted
  transaction boundaries and all 511 partial-append positions.
- Formatting and `git diff --check`: passed.
- Final `gh_release_rc` build: passed; static RAM 66,696/327,680 bytes,
  flash 5,493,963/6,553,600 bytes.
- Final `x4pro-gh_release_rc` build: passed; static RAM 111,756/327,680 bytes,
  flash 5,350,302/6,553,600 bytes.
- FreeInk SDK remains pinned at
  `44427f8d51866848b26bea01197f499f9fee5c4b`, without local SDK changes.

Static build sizes are not runtime heap measurements. Host storage models do
not establish physical FAT/SD durability. Build logs, source patches and binary
checksums were saved locally in `artifacts/ybx-sleep-recovery-20261028` in the
parent YBX workspace; generated binaries and logs are not committed here.

## Outstanding

- Four independent final reviews are incomplete. OpenCode Go correctness,
  architecture and embedded attempts timed out without substantive results;
  Qoder documentation review failed on its credit limit. An earlier Home
  architecture review does not cover all final changes.
- Synchronous SDK DNS/TCP and SD operations can exceed the cooperative sleep
  budget. No hard upper bound on sleep latency is claimed.
- The inherited 4 KB outbox overflow policy still drops the queue wholesale;
  shared attempt limits and subscription order can starve later plugins.
- Native account statistics and achievements are planned, not implemented.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- Inherited HTTPS certificate verification remains disabled. Transport/OTA
  trust hardening is a separate release blocker.
- No device flashing, hardware validation, release or PR is part of these checks.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

## Settings / Updates / About parity lane (same date, separate change)

The numbers above belong to the sleep-recovery lane. For the Settings / Updates /
About reconciliation ([settings parity](ybx-settings-parity.md)):

- `~/.platformio/penv/bin/pio run -e default` — **passed**; static RAM
  66,712/327,680 bytes, flash 5,545,169/6,553,600 bytes, i18n blob 408,093 bytes.
  Re-run after formatting gave identical figures. Full log `/tmp/fsp-build4.log`
  (not committed).
- `./bin/clang-format-fix -g` — **passed**, and idempotent on the second run. The binary
  is not on `PATH` here and the script rejects anything below 21, so the gate ran with the
  clang-format **21.1.0** bundled in the installed Python `clang_format` package linked onto
  `PATH`; no repository file was changed to make that work. It re-packed the new `menuNames`
  table, which is the only formatting change it made.
- Host suite (CTest under `test/`) — **898/898 passed**, configured offline
  (`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<v1.17.0 copy>`, `-DANIMATEDGIF_SOURCE_DIR=<copy>`)
  because both `FetchContent` clones stall in this environment. Log `/tmp/fsp-host2.log`.
  The changed code is activity rendering and input handling, which the host harness does not
  cover, so this is a no-regression signal only.
- Four independent read-only handoff reviews (correctness, architecture, embedded,
  i18n/docs) ran against the candidate, plus a second targeted round over those fixes.
  Confirmed findings are all resolved in the same change: About position-sync gating, the
  unlabeled touch tap that could start a re-flash, the stale progress counters on a retried
  install, two doc citations, and one check that had been recorded as passing when it had
  not been run. Left open deliberately and written up in the parity document: the native
  support-log upload (blocked on the server auth contract), the whole-partition erase per
  retried install, and a visible on-screen Retry control for touch devices
  (`WifiSelectionActivity.cpp:926-941` is the precedent). Nothing in the second round was
  re-reviewed after the final formatting and progress-counter edits; those are mechanical,
  main-agent verified, and covered by the build and suite above. No device flashing or
  hardware validation is part of these checks.

## Owner decisions on the open OTA items + review follow-up (2026-10-29)

The two deliberately-open OTA items above were decided by the owner (recorded with the
rationale in [settings parity](ybx-settings-parity.md), "Owner decisions"):

- The FAILED screen now draws labeled on-screen Back/Retry controls on every profile,
  touch boards included; a tap outside the controls does nothing, physical Back/Confirm
  keep working, and a wrong-device failure offers Back only.
- The whole-slot erase stays, covered by a new "Preparing…" state (no fake percentage)
  until `installUpdate()` reports the erase done, then the normal progress bar.

A parallel broad review of the committed parity change additionally reported one blocking
defect, confirmed in the code and fixed here: a retried update check that resolves to
"no update" set the state under `RenderLock` without requesting a frame, so the panel kept
showing "Checking for update…" (the first check relied on ActivityManager's post-pop
repaint; the retry path runs inside `loop()` and has none). `requestUpdate()` now follows
both `state = NO_UPDATE` assignments. The review's non-blocking notes were applied as: the
About state values are documented as deliberately English (comment extended, no new keys —
owner decision keeps About support-facing); an on-screen Retry for touch is now implemented
above; error-classification tuning (e.g. `INTERNAL_UPDATE_ERROR` still offers Retry) was
left as-is — it is a suggestion, not a confirmed defect.

- `~/.platformio/penv/bin/pio run -e default` — **passed**; static RAM
  66,712/327,680 bytes (unchanged from the first pass), flash 5,546,745/6,553,600 bytes,
  i18n blob 408,200 bytes (generator total; +107 over the first pass — one new key:
  70 B of offset tables plus its English and Russian strings). Full
  log `/tmp/fsp-build5.log` (not committed).
- Host suite (CTest under `test/`) — **898/898 passed**, configured offline as
  above; the changed code is activity rendering/input, which the host harness does not
  cover, so this is a no-regression signal only.
- `PATH=<clang-format 21 venv> ./bin/clang-format-fix -g` over the C/C++ files of both
  parity passes (`OtaUpdateActivity.cpp/.h`, `AboutActivity.cpp/.h`, `OtaUpdater.cpp`) —
  **passed** with zero file changes, so the build above is the build of the committed
  sources.
- Owner hardware checklist for these decisions: steps 1–5 and 9 of the "Owner hardware
  checklist" section of [settings parity](ybx-settings-parity.md) (updated for the
  on-screen controls and the "Preparing…" state). No device flashing or hardware
  validation is part of these checks.

## OTA failure-screen review follow-up (2026-11-01)

Five findings were reported against the failure screen. Four were confirmed in the code and
fixed; the fifth (key-mapping-derived Retry) was half right — the labels already follow the
front-button mapping, but the keys acted on the press edge, which handed their own release
to the Cancel/Update popup a retry opens (that popup defaults to Update, so the release
started a slot-erasing install nobody chose). The review body reached this session
truncated — only the five finding titles survived — so each row below is what the code
showed once the title was checked against it, not a quotation of the report. Details,
including why `confirmPopup` is deliberately *not* mutated under `RenderLock`, are in
[settings parity](ybx-settings-parity.md), "What this branch changed" items 1–2 and
"Error states: exit and retry".

| Finding | Verdict | What the code showed |
| --- | --- | --- |
| render under lock | **confirmed** | `runUpdateCheck()` published `state = WAITING_CONFIRMATION` under `RenderLock` and only *then* filled `confirmPopup`, so the render task could enter that branch and paint the popup while the loop task was inside `show()` resizing `ownedStrings` — a reallocation under the reader. The options are now filled before the state is published (the lock release gives the render task the ordering edge). Every other write of render-visible state on the failure path (`state`, `failedDetail`, `allowRetry`, `failureStage`, `lastUpdaterPercentage`) was already under the lock and stays that way. `confirmPopup.handleInput()` is *not* wrapped in the lock: it can run the selection callback, which calls `runUpdateInstall()`, which takes the lock, and `renderingMutex` is a plain non-recursive `xSemaphoreCreateMutex()` (`ActivityManager.cpp:40`, `:516-521`; `EpubReaderActivity.cpp:220` records the same) — locking there would self-deadlock. Kept as a comment. |
| key-mapping-derived Retry | **partly confirmed** | The label and the key were never inconsistent: `mapLabels()` and `wasPressed`/`wasReleased(Button::…)` both resolve through the same `SETTINGS.frontButton*` mapping. The defect under that title was the edge — FAILED handled `wasPressed(Confirm)`, so the *release* of that same keypress reached the Cancel/Update popup the retry opens, whose selection defaults to Update: an unchosen slot-erasing install. Both keys now act on release, matching `OptionPopup::handleInput` and `ui-activities.md` ("Confirm/Back callers retain release/long-press semantics"). Routing keys through the interaction table was rejected: `touchSnapshotFrom()` carries touch only and SDK key routing needs focus, which nothing here does this way. The pair-order comment that claimed it matched "the physical Back/Retry hints order" was wrong (the hints follow the mapping, which can be swapped) and now cites the dialog convention it does match (`UiAppHelpers.h:151-172`). |
| Wi-Fi reconnect path | **confirmed** | Retry re-ran the failed step in whatever connection state existed; if the station dropped while the screen was up (AP reboot, out of range) it failed again the same way and the button looked dead. `retryFailedStep()` checks `WiFi.status()` and `launchWifiSelection(FailureStage)` (`ClockSyncActivity.cpp:42-46` is the in-repo pattern) re-opens network selection, then resumes the stage that failed. This replaces `onWifiSelectionComplete(bool)`, which always restarted the lookup even after an install failure. |
| clamped buttons | **confirmed** | `failedScreen()` used `{body.x, body.y + body.height * 2 / 3, body.width, theme.rowHeight}`: a fixed themed row height, so whenever the band's lower third is shorter than one control (landscape, small safe area) the control was drawn past the band into the hint bar. The pair also computed `(band.width - gap) / 2`, which goes negative when the themed gap exceeds the band width, and those `int16_t` widths reached `fui::button` as-is. `otafailure::layout()` shrinks the height to the room left, caps the gap at a third of the band, and reports "no control" rather than drawing one that leaves the band. Residual, stated in the parity doc: `fui::button` still expands each *hit* rect to the SDK minimum touch target, clamped to the screen rather than the band (`components/controls/button.h:45`). `UiAppHelpers.h:155-171` keeps the same unclamped pattern for the slider Cancel/OK pair on touch devices and was left alone as out of scope. |
| reason for internal errors | **confirmed** | `INTERNAL_UPDATE_ERROR` (no OTA partition, `esp_ota_begin`/`write`/`end` refusing) fell through to a bare "Update failed" heading with no reason line, and the install path named only two of the seven codes. The reason and the retry policy now come from one table (`otafailure::classify()`), which covers every code, including the two that were silently "no reason": a release JSON that could not be read, and a release that is not newer. Retry stays on for the write failure (worth re-running) and off for the wrong-device and not-newer cases. |

Also fixed in the same path: the progress percentage divided by `getTotalSize()` without
checking it, so an asset whose size the release JSON never reported produced `inf` →
`static_cast<int>` (UB) and a bar value past its own maximum; the value is now guarded and
clamped to 0…100.

- Failure reason and retry policy now come from one table, and the control geometry from one
  helper, both in `src/util/OtaFailureScreen.h`; the FAILED branch of the activity reads them.
  Every reason reuses an existing `STR_*` entry, so **no translation key was added** and the
  i18n blob is unchanged.
- Retry re-enters the step that failed, and if the station dropped while the failure screen
  was up it goes through `WifiSelectionActivity` first and resumes that same step
  (`launchWifiSelection(failureStage)`, replacing `onWifiSelectionComplete(bool)`).
- The on-screen Back/Retry pair is sized to the space the content band really has instead of
  assuming one themed row fits; if the band holds no control, the screen draws none.
- The progress bar no longer divides by an unset `totalSize` (a release asset with no size
  reported `0 / 0`, and the percentage was unclamped).

- `~/.platformio/penv/bin/pio run -e default` — **passed**; static RAM 66,712/327,680 bytes
  (identical to the previous pass), flash 5,547,181/6,553,600 bytes (+436 over the previous
  pass), i18n blob 408,200 bytes (unchanged — no new key). Full log `/tmp/ota-fix-build.log`
  (not committed).
- Host suite (CTest under `test/`) — **906/906 passed**, 0 failed: the 898 previous cases
  plus 8 new `test/ota_failure/` cases over the reason table (each error names a reason,
  only repeatable failures offer Retry) and the control placement (pair splits the band,
  centered Back, short/narrow/empty bands). Configured offline as before
  (`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<v1.17.0 copy>`). The rendering and touch routing of
  the screen itself stay outside the host harness.
- `PATH=<clang-format 21 venv> ./bin/clang-format-fix -g`, plus `clang-format --style=file
  --dry-run -Werror` over the four touched C/C++ files (including the two new untracked ones,
  which `git ls-files` does not see) — **clean**, zero changes.
- Note for the owner: a full-tree `clang-format -g` run in this environment re-packed one
  already-committed `setContentMarginFromScreen` call in `OtaUpdateActivity.cpp` onto one
  line. That is the formatter's canonical output for the region this change edited, not a
  hand edit.
- Not verified by these checks: any hardware behaviour. Steps 1–5, 9 and the new step 11 of
  the [settings parity](ybx-settings-parity.md) owner checklist cover this pass — including the
  release-edge Confirm (it must not auto-select Update), the retry after the connection
  drops (must open network selection, not fail again instantly), the "no newer release"
  screen (Back only), the buttons staying inside the band after an orientation change, and
  each failure reason line appearing translated.

