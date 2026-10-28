# YBX Settings / Updates / About parity on the native Xteink interface

Owner request, 2026-10-28: reconcile the working Settings / Updates / About screens of
the YBX Classic and Desktop clients with the firmware's native screens — sync toggles,
book folder, language/appearance, start screen, version/updates, support and the
diagnostic log. Reuse existing firmware settings and OTA; do not port InkView/Qt
options that have no meaning on Xteink.

This document is an inventory and a decision record. It does not claim that any
item is hardware-accepted; the owner checklist is at the end.

## References read (read-only, nothing outside this repository was changed)

| Reference | Revision | What was read |
|---|---|---|
| `ybx-apps/apps/pocketbook-qt/qml/{SettingsPage,UpdatesPage,AboutPage}.qml` | `0dcdbf7c24ff35c11d880f20a4b3d3e549a41377` | the controls each screen actually renders |
| `ybx-apps/apps/pocketbook-classic/ybx_inkview_app.c` | same | persisted toggles: `own_reader`, `stock_progress`, `stock_write`, `kosync_sync`, `litres_sync`, `home_page`, `diagnostic_log`, log path (`:900-935`) |
| `YBX Site/src/xteink_yandex_bridge/{app.py,pocket_support.py}` | `8bfc191b3aeae63f140eb615ad5f7330cfc6631e` | support-log route and its device authentication |
| `sd-card/plugins/ybx/{plugin.js,device.json}` | this repo | the Xteink YBX plugin: folder, token, KOSync setup |

[Client parity](ybx-client-parity.md) cites the older `ybx-apps` snapshot
`0e24398…`; the line numbers above are from the current checkout, so they may move.

## One source of truth already exists for settings

`src/SettingsList.cpp` descriptors feed both the on-device section list
(`src/activities/settings/SettingsActivity.cpp:97`) and the browser settings page
served by the device (`src/network/CrossPointWebServer.cpp:235-237`; `:1404` and
`:1513` skip `webHidden` entries). Native and desktop-web settings therefore cannot
drift into two implementations: a difference is only a flag (`category`,
`webHidden`, `inTextSettings`). The rows below are the reference controls, not a
second settings model.

## Matrix

Status key: **reused** = already works natively; **extended** = this branch added the
missing piece; **not copied** = deliberate; **blocked** = needs a decision outside
this repository.

| Reference control | Native firmware surface | Status |
|---|---|---|
| Sync switch "progress on this reader" + "sync now" (`SettingsPage.qml:176,265`; Classic `kosync_sync` `:919`) | KOReader/KOSync setup rows: username, password, server URL, server type, two headers, document matching, send metadata, sync behaviour, clippings, sign-up, authenticate (`src/activities/settings/KOReaderSettingsActivity.cpp:34-36`). Every client call is gated on credentials (`lib/KOReaderSync/KOReaderSyncClient.cpp:62,96,135,220`) and the reader menu only offers Sync when they exist (`src/activities/reader/EpubReaderMenuActivity.cpp:96`); "sync now" is the user action — long-press function, Home button action, or menu entry (`src/activities/reader/EpubReaderActivity.cpp:849-852,884-886,1271-1273`). Plugin reading events have their own master toggle, the device Plugins popup (`src/activities/settings/SettingsActivity.cpp:382`; the setting is `webHidden`, `src/SettingsList.cpp:195`) | **reused** — two real switches already exist (credentials, plugin events); a third boolean would be a second source of truth |
| Per-provider progress switches (Classic `litres_sync` `:920`) | none — no Litres account exists on Xteink | **not copied** |
| Interface language (`SettingsPage.qml:276`) | `STR_LANGUAGE` action row → native language picker (`SettingsActivity.cpp:110`); 35 languages in `lib/I18n/translations` | **reused** |
| Appearance/theme, dark palette (`SettingsPage.qml:381`) | home theme enum in the Display category (`src/SettingsList.cpp:52-53`, `CrossPointSettings.h:205`), Night Mode inversion (`src/SettingsList.cpp:68`), reader typography and status-bar rows (`src/SettingsList.cpp:327-348`) | **reused** — the reference palette switch maps to Night Mode, not to a second palette system |
| Home page chooser (`SettingsPage.qml:325`, Classic `home_page` `:921`) | fixed native home layouts (`src/components/YbxHomeUi.cpp`, `CoverGridHomeUi.cpp`) chosen by `uiTheme` | **not copied** — a page chooser would duplicate the theme picker |
| Book folder (folder dialog, `SettingsPage.qml:461,533` with `Config.booksDir`) | downloads land on fixed roots: the YBX plugin writes to `/books/ybx` (`sd-card/plugins/ybx/device.json:39`); OPDS `opdsDownloadFolder`/`opdsFilenameFormat` are persisted and web-exposed but category-less, so they stay out of the device list by design (`src/SettingsList.cpp:213-221`, `src/CrossPointSettings.h:342,346`) | **reused** — choosing a folder on the device means hand-typing a path; the web settings page already edits it |
| App telemetry switch "errors and launches, no book names" (`SettingsPage.qml:398`) | none | **not copied** — `SCOPE.md:35` closes new external connectors and "talk to a server" features without an owner decision |
| Stock-progress switches (`SettingsPage.qml:413,423`; Classic `stock_progress`/`stock_write` `:901-904`) and `own_reader` (`:900`) | none | **not copied** — they choose between the YBX reader and the PocketBook stock reader, which does not exist on Xteink |
| Diagnostic log switch (`SettingsPage.qml:435`; Classic `diagnostic_log` `:933-934`, `<app data>/ybx.log`) | RAM/RTC-backed log ring already reaches the crash report (`lib/Logging/Logging.cpp:77-90`, `lib/hal/HalSystem.cpp:174`); `LOG_LEVEL` is 0 in release (`lib/Logging/Logging.h:31`), so only errors are compiled in | **not copied (yet)** — a switch would change the release build's log verbosity, which is a build-configuration decision |
| Send log + receipt (`SettingsPage.qml:445-446`) | see "Support log" below | **blocked** |
| Updates entry (`SettingsPage.qml:469` → `UpdatesPage.qml`) | OTA over the YBX release channel plus SD flashing (`SettingsActivity.cpp:146-147`, `src/activities/settings/OtaUpdateActivity.cpp`, `SdFirmwareUpdateActivity.cpp`) | **extended** — failure states now have a working exit and Retry (see below) |
| Beta channel switch (`UpdatesPage.qml:101`) | single channel: `https://api.github.com/repos/jhdvp/ybx-crosspoint/releases/latest` (`src/network/OtaUpdater.cpp:22`), asset `ybx-<tag>-<board>.bin` (`:44-57`) | **not copied** — no beta assets exist; adding a switch that can never find a build is a dead end |
| Open download page (`UpdatesPage.qml:93`) | SD firmware update covers the recovery case where OTA cannot run | **reused** |
| About page contents (`AboutPage.qml:94-116`: capabilities, safety, TLS state, catalog host, book dir, OS) | `AboutActivity.cpp:90-130`: device profile, firmware version, chip, flash, display controller, resolution, touch, frontlight, RTC, IMU, MAC, and now position-sync state | **extended** — hardware identity is what support needs from a photographed screen |
| Unlink reader from account (`SettingsPage.qml:487`) | pairing/unlink belongs to the plugin account flow (`sd-card/plugins/ybx/plugin.js`), tracked by the pairing row of [client parity](ybx-client-parity.md) | **not copied here** |

## What this branch changed

1. **OTA failure states have an exit and a Retry.** The check step was extracted into
   `runUpdateCheck()` (`src/activities/settings/OtaUpdateActivity.cpp:32-88`) so
   `retryFailedStep()` (`:334-340`) re-enters the step that actually failed instead of
   always restarting the release lookup, while the Wi-Fi connection this activity opened
   is still up. `failureStage` records the step; `allowRetry` disables Retry for the one
   failure that repeats identically (a wrong-device image), so that screen offers Back
   only. The FAILED screen draws labeled on-screen controls — Back left, Retry right
   (`failedScreen()`, `:260-306`) — on every profile, including touch boards where the
   hint bar is suppressed (`BaseTheme::drawButtonHints` returns when `gpio.hasTouch()`).
   Taps route only to those controls; a tap anywhere else does nothing (`:350-366`).
   Physical Back always exits, physical Confirm retries while Retry is offered, and the
   header chevron folds into Back like on every other screen.
2. **Failures say why.** A detail line under "Update failed" now distinguishes out of
   memory, a network problem (`STR_UPDATE_NETWORK_HINT`, new in
   `lib/I18n/translations/english.yaml` and `russian.yaml`), and a wrong-device image
   (`:59`, `:233-244`).
3. **About reports position-sync state.** New "Position Sync" row is keyed on
   credentials first — `On`, or `No login` when a server URL was set without a login,
   or `Off` when neither exists (`src/activities/settings/AboutActivity.cpp:108-112`).
   An empty URL is *not* "off": `KOReaderCredentialStore::getBaseUrl()`
   (`lib/KOReaderSync/KOReaderCredentialStore.cpp:151-160`) substitutes the built-in
   `https://sync.crosspointreader.com`, and every sync call gates on
   `hasCredentials()` (`lib/KOReaderSync/KOReaderSyncClient.cpp:62`), so testing the URL
   first would print `Off` on a device that is syncing. The server URL is
   deliberately **not** shown: `UrlUtils::extractHost` (`src/util/UrlUtils.cpp:35-47`)
   keeps the userinfo part of a hand-typed URL, so `https://user:pass@host` would print
   credentials on a screen meant to be photographed for support.

No new setting key, no new persistent state, no new heap-resident list, no new network
connector. Flash cost of the added string table entries: 407,926 B → 408,093 B
(i18n generator output) for the first pass; the second pass's figures are recorded in the
"Settings/Updates/About parity" section of [validation status](ybx-validation-status.md).

## Owner decisions (2026-10-28)

**1. The failure screen has labeled on-screen Retry/Back controls.** The owner accepted
drawing two explicit buttons, «Повторить» / «Назад» (Retry / Back), on every profile —
touch boards included, following the in-repo on-screen control precedent in
`WifiSelectionActivity.cpp:926-941`. A tap on a control performs its action; a tap
anywhere else does nothing at all (it neither exits nor retries), because an unlabeled
tap must never start a slot-erasing re-flash. Physical Confirm still retries and
physical Back still exits. A wrong-device failure offers Back only, because repeating it
cannot help.

**2. The whole-slot erase is kept, with a visible "Preparing…" face.** `installUpdate()`
opens the slot with `esp_ota_begin(..., OTA_SIZE_UNKNOWN, ...)`
(`src/network/OtaUpdater.cpp:166`), which erases the whole ~6.25 MB app partition before
byte 0. Passing the already-parsed `getOtaSize()` would shrink that, but it moves the
flash-write path onto a size the release asset reports rather than the bytes actually
received, and neither this repository nor the host suite can prove it on hardware. The
owner chose to keep the erase and make it visible instead: the activity enters
`UPDATE_PREPARING`, paints «Подготовка…» / "Preparing…" with no percentage while the
erase runs, and switches to the normal progress bar when `installUpdate()` reports the
erase is done (`OtaUpdater.cpp:172-175`, `OtaUpdateActivity.cpp:214-231`).

## Support log: why the native upload is not shipped

The request said to reuse the server support-log contract and to send diagnostics only
by an explicit action, with secrets/book content edited out and a receipt. The contract
does not accept what the firmware holds:

- The route is `POST /api/v1/devices/{device_id}/support-log`
  (`YBX Site/src/xteink_yandex_bridge/app.py:8799-8803`) and it is gated by
  `require_pocket_device` (`:2301-2316`), i.e. `_authenticate_device(..., pocket_app=true)`.
- That authenticator requires HTTP Basic whose **password is not a `cp_` token**
  (`:2224-2226`: `not credentials.password.startswith("cp_")`) and whose username
  normalizes to the device id. The support-log rows are the PocketBook app's contract
  (`pocket_support.py:32,112`, receipt id `sl_…`).
- The Xteink side holds only the scoped CrossPoint device token (`cp_…`;
  `crosspoint_tokens.py:35,40` mints it with the scopes `catalog download events`, and
  the KOSync audience accepts it only as a Basic password, `app.py:8617-8633`), never a
  device key. Reusing it for support would mean either a new server audience or a
  firmware that stores a second, stronger secret — both are owner decisions, and the
  support table itself is still waiting on the V18 rollout
  (`YBX Site/docs/ROLLOUT_SUPPORT_LOG_V18.md`).

Shipping a "Send diagnostic log" row against that would produce a button whose only
possible outcome is an authorization error. Per the requirement not to declare a menu
item ready, the item stays open here; a native submission needs, in this order: a server
audience/scope decision for `cp_` tokens or a separate support credential, an
explicit-action UI with an editable/previewable redacted payload, and the receipt shown
back to the user. The scrubbing rule is already visible in the firmware's own data
choices: passwords are never pre-filled on the web pages, the KOSync password is stored
obfuscated, and About shows sync state instead of the URL.

## Error states: exit and retry

The requirement was that error, empty, no-network and expired-authorization screens
have a working exit and a retry. Where each stands now:

- **OTA** — the FAILED screen is the one place a same-screen Retry is honest: this
  activity owns the Wi-Fi connection and keeps it, so Retry re-enters the failed step
  (`OtaUpdateActivity.cpp:334-340`). Retry is withheld when the failure would repeat
  identically (wrong-device image), leaving Back as the only exit. Both actions are
  labeled on-screen controls on every profile (Back left, Retry right), so touch devices
  have a visible way to act; the physical Back/Confirm keys do the same and keep working.
  A tap outside the controls does nothing (`:350-366`) — no exit and no re-flash. A retry
  resets both progress counters (`OtaUpdater.cpp:160-163`, `OtaUpdateActivity.cpp:202-212`)
  so the screen leaves the previous attempt's numbers, and the slot erase is covered by
  the "Preparing…" state instead of a 0 % progress bar.
- **Position sync** — `KOReaderSyncActivity` shows the reason under a translated
  "Sync failed" heading and its Back/Confirm (plus auto-return) all leave the activity
  (`:723-731`, `:734-744`). A Retry button was **not** added: the failure path already
  stops the radio (`:427`) and `onExit()` performs the fragmentation cleanup with a
  silent restart (`:478-484`), so a same-screen retry would reopen Wi-Fi inside a screen
  whose contract is "one network session". Re-opening Sync from the reader menu is the
  retry, and it re-establishes the connection (`:464-475`).
- **Known gap, left open deliberately**: the sync detail line is
  `KOReaderSyncClient::errorString()` — eight hardcoded English literals
  (`lib/KOReaderSync/KOReaderSyncClient.cpp:304-324`), so a Russian device shows an
  English reason under a translated heading. Translating it properly means adding keys
  for network/server/JSON/not-found states across 35 languages, which is a separate
  i18n task rather than part of this parity pass. The two keys that already exist
  (`STR_MEMORY_ERROR`, `STR_AUTH_FAILED`) are not reused, because half-translated
  detail lines on one screen are worse than consistently diagnostic English.

## Build and checks on this branch

- `~/.platformio/penv/bin/pio run -e default` — passed. The numbers for *this* branch are
  in the "Settings/Updates/About parity" record of
  [validation status](ybx-validation-status.md); the earlier figures in that file belong
  to the sleep-recovery lane.
- `./bin/clang-format-fix -g` — **passed** (re-run confirms it is idempotent: the second
  run rewrites nothing). `clang-format` is not on `PATH` in this environment and the script
  refuses anything older than 21; the gate was satisfied by putting the clang-format **21.1.0**
  that ships with the installed Python `clang_format` package on `PATH` first
  (`/tmp/fsp-bin/clang-format-21` symlink, nothing in the repository changed). It re-packed
  the `menuNames` table in `AboutActivity.cpp` to its canonical column alignment; the rebuild
  after that whitespace-only edit produced byte-identical size figures.
- Host suite (`pio run -t unit-tests` / CTest under `test/`) — **898/898 passed**,
  0 failed. Both `FetchContent` downloads stall in this environment (googletest,
  AnimatedGIF), so the suite was configured offline against complete local copies instead:
  `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<v1.17.0 source>` plus the documented
  `-DANIMATEDGIF_SOURCE_DIR=<pinned source>` (`test/gif_support/CMakeLists.txt:2`). That is
  the same pinned tag the fetch would use, so the suite ran unchanged; nothing in the
  repository was modified for it. The touched code is activity rendering and input
  handling, which the host harness does not cover — no host test was added for it, so this
  number is a no-regression signal, not evidence about the new screens.

## Owner acceptance checklist (device steps, not performed by the agent)

1. Settings → General → Check for updates, on a network that joins but has no internet
   (router with the WAN cable unplugged). Expect: "Update failed" with "Check the network
   and try again" and two labeled buttons below the text — Back on the left, Retry on the
   right (on buttons-only profiles the hint bar still shows Back / Retry as well). Press
   Back → returns to Settings, no reboot loop, Wi-Fi is torn down. Restore internet, enter
   again, reach the failure, then either tap Retry or press Confirm → the lookup re-runs
   without asking for Wi-Fi again.
2. Same failure screen, leave it up and wait for auto-sleep. Expect: sleep is not blocked
   by this state (`preventAutoSleep()` covers only checking/preparing/installing) and
   Back still exits after wake.
3. Rename/serve a firmware asset built for a different board (or reuse a wrong-device
   `.bin` on the SD path). Expect: "Update failed" plus the wrong-device line and a single
   centered Back button; Retry must not be offered, and physical Confirm must do nothing.
4. On a touch profile (X4), reach the retryable failure of step 1. Tap (a) Retry — the
   install must start; (b) after it fails again, Back — the screen must exit; (c) after
   re-entering the failure, tap the heading, the gap between the buttons, or empty space —
   the screen must do nothing at all (no exit, no download). The header back chevron is a
   Back control, like on every screen. Check that Retry is not reachable by an unlabeled
   tap anywhere else on the panel.
5. Watch an install (first attempt or a retry after a download failure). Expect, in order:
   "Preparing…" (`Подготовка…` in Russian) with no percentage for the slot erase — tens of
   seconds, no progress bar — then "Updating…" with `0 / <size>` and a real bar from 0 %,
   never the previous attempt's percentage. See the owner decision above.
6. Settings → General → About on X3 and, if available, on another profile. Expect:
   12 rows, "Position Sync" = `Off` when nothing was ever configured (empty URL and no
   credentials — the empty URL still means the built-in default server, but without a
   login no sync runs), `No login` once a server URL is typed without a username,
   `On` as soon as username and password are saved — including with the URL field left
   empty, which syncs against the default server; no URL text on screen.
7. Photograph the About screen and check the image: no password, no server URL, no book
   title.
8. Settings → Network → KOReader Sync, then Settings → Network → Plugins. Expect the two
   real switches: without credentials the reader menu offers no Sync entry and About
   reads `No login` (typed URL) or `Off` (nothing configured); with credentials Sync
   transfers position. Disabling Plugins stops reading events reaching the YBX plugin
   while the explicit native sync still works —
   this is where the Classic "sync on this reader" switch maps.
9. Update the device language to Russian and revisit Check for updates. Expect: the
   network hint and «Подготовка…» appear translated, the failure screen's buttons read
   «Назад» / «Повторить», and About stays English (support-facing labels and values).
10. With KOSync credentials configured, open a book and trigger Sync on a network with no
   internet. Expect: the heading "Ошибка синхронизации" plus the English reason line,
   Back or Confirm returns to the reader, and the device then sleeps
   normally. Restore internet and trigger Sync again — that re-entry is the retry path.
