# YBX unified → 2.0.0rc port map (stage 1)

Branch: `port/ybx-unified-on-2rc-20261105` (worktree `~/Projects/YBX/.worktrees/ybx-unified-port-2rc-20261105`)
Source of features: `origin/integrate/ybx-unified-20261104` (PR #23)
Target base: `origin/ybx/2.0.0rc` (`c4816777`)
Common base of PR #23: `feat/ybx-native-integration` — **no shared history with 2.0.0rc**; merge/rebase are impossible, port is file-by-file.

Diff under port (`git diff --name-status feat/ybx-native-integration origin/integrate/ybx-unified-20261104`):
**281 files, +25 010 / −827, 70 commits.**
(Note: the earlier triage report said "54 patches / +18k" — that count is an undercount; the real diff is 281 files.)

## Classification rule

- **A (mechanical take)** — the file is absent in 2.0.0rc, or 2.0.0rc's copy is byte-identical to the native base. In both cases taking PR #23's version applies exactly the #23 delta with no manual reconcile.
- **B (manual reconcile, stages 2..N)** — the file changed in 2.0.0rc independently of PR #23. Both trees diverged from the base; a blind take would revert 2rc work or break the build.

Class-A files were ported one subsystem per commit, with `pio run -e default` green after **every** commit.

## Result

**Ported (A): 206 files of 281. Left (B): 75 files.** Firmware build green at every commit (final: `SUCCESS`, RAM 21.2 %, Flash 84.9 %).

| Subsystem | A (taken) | B (left) | Port commits |
|---|---|---|---|
| repo/ci (scripts, bin, .gitignore) | 11 | 3 (README.md, USER_GUIDE.md, .github/workflows/ci.yml) | `a4f057ea` |
| docs | 46 | 4 (file-formats, plugin-events, sd-plugins, webserver-endpoints) | `fe9a136a` |
| lib (JsonParser, TrustedTime, Txt) | 6 | 5 (TxtToHtml.cpp, Epub ParsedText/Section/TextBlock) | `d221dda0` |
| i18n (lib/I18n/translations) | 3 (union) | 0 | `f413ad13` |
| sd-plugin (ybx 1.9.1 + config.json) | 6 | 0 | `22279555` |
| nativeapps | 15 | 0 | `8aee728c` |
| util (incl. network policy headers) | 27 | 9 | `fc866df9` |
| network + clippings | 5 | 12 | `fc866df9` |
| components | 2 | 2 | `35d306d7` |
| activities | 10 | 34 | `540d1aca` |
| src root (ClippingStore, main.cpp) | 2 | 1 (main.cpp) | `fc866df9` |
| tests (host, CMake) | 73 | 4 | `a532ff98` |
| platformio.ini | — | 1 | — |
| **Total** | **206** | **75** | 10 commits |

### Special cases inside the A set

- **i18n union (`f413ad13`)** — #23's yaml deltas are purely additive, but 2rc diverged from the base (english −22/+4: deleted old `STR_YBX*`/`STR_PLUGIN_UNINSTALL*`/`STR_SPACING_*` keys, added `STR_NOT_IN_PREVIEW`, `STR_TIGHT`, `STR_WIDE`, `STR_EXTRA_WIDE`). Union = #23's yaml + the four 2rc-only keys re-appended per language. The i18n generator accepts the superset; nativeapps' 23 missing `STR_YBX_*` strings resolved.
- **Transitive deps not in the 281 diff** (unchanged by #23, i.e. base == #23, but absent in 2rc): `src/util/PluginEventDeadline.h` (needed by `YbxReadingData.cpp`, kept for stage 2) and `sd-card/plugins/ybx/config.json` (plugin sleep-cover size 480×800, taken with the plugin bundle).

## Class-B files (stages 2..N), grouped by subsystem

### Blocking reasons found by the build

1. **freeink-sdk pin** — 2rc pins `f9cc1be`, PR #23 pins `44427f8`. The pin itself is *not* in the 281 diff and was **not** bumped in stage 1. `freeink::SecureClient::ConnectFailure` (libs/network/SecureNet/include/SecureClient.h) and `FontCacheManager` (libs/ui) exist only in #23's pin. Until the SDK pin is reconciled (or `scripts/patch_securenet.py` is applied via `platformio.ini`), these files cannot compile.
2. **pluginhttp `timeoutMs`** — 2rc deliberately removed the trailing `shouldAbort`/`timeoutMs` params from `pluginhttp::request/requestToFile/mintPasswordToken`; #23 keeps and uses them. Callers of the #23 signature fail with "too many arguments".
3. **UITheme::getHeaderRect** — 2rc removed it from `UITheme.h/.cpp` (header-layout rework); #23 code calls it.
4. **ReaderSession API** — #23 changed the reader-session API (`noteTurn(succeeded)`, `summary()`, `pause()`, `finish()`, `ReaderSessionSummary`); 2rc's copy still uses the base API.
5. **Both-sides rework** — file changed independently in 2rc and #23; needs a manual semantic merge (no specific compile blocker recorded).

### network + clippings (12)

| File | Reason |
|---|---|
| src/network/TlsPolicy.cpp | SDK pin (ConnectFailure) |
| src/network/TlsTransport.h | SDK pin (ConnectFailure) |
| src/network/TlsRoots.h | SDK pin (ConnectFailure) |
| src/network/HttpDownloader.h | SDK pin; #23 adds FetchOptions/shouldAbort/postForm |
| src/network/HttpDownloader.cpp | same header contract |
| src/network/OpdsHttpTransport.{h,cpp} | SDK-coupled to HttpDownloader #23 contract |
| src/network/LcpDevice.{h,cpp} | SDK-coupled |
| src/network/OtaUpdater.cpp | paired with HttpDownloader #23 contract + TLS_ERROR |
| src/network/CrossPointWebServer.cpp | both-sides rework |
| src/clippings/ClippingSync.cpp | includes network/TlsTransport.h (SDK pin) |

### util (9)

| File | Reason |
|---|---|
| src/util/PluginHttp.{h,cpp} | 2rc removed timeoutMs; #23 uses it — reconcile signatures |
| src/util/YbxReadingData.{cpp,h} | calls pluginhttp with timeoutMs (compile error: "too many arguments") |
| src/util/ReaderSessionJournal.{h,cpp} | ReaderSession API rework |
| src/util/PluginEvents.{cpp,h} | both-sides rework |
| src/util/FirmwareIdentity.h | both-sides rework |

### activities (34)

| File | Reason |
|---|---|
| src/activities/reader/ReaderSession.{h,cpp} | ReaderSession API (breaking signatures) |
| src/activities/reader/ReaderActivity.{h,cpp} | ReaderSession API |
| src/activities/reader/EpubReaderActivity.{h,cpp} | both-sides rework |
| src/activities/reader/EpubReaderMenuActivity.{h,cpp} | both-sides rework + UITheme::getHeaderRect |
| src/activities/reader/KOReaderSyncActivity.cpp | both-sides rework + UITheme::getHeaderRect |
| src/activities/CatalogActivity.{h,cpp} | FontCacheManager missing in 2rc SDK pin (compile error) |
| src/activities/settings/OtaUpdateActivity.{h,cpp} | tlsui::failureDetail / TlsTransport / OtaUpdater::TLS_ERROR (SDK pin) |
| src/activities/settings/AboutActivity.{h,cpp} | both-sides rework |
| src/activities/settings/FontDownloadActivity.cpp | both-sides rework |
| src/activities/ybx/YbxAccountActivity.{h,cpp} | pluginhttp timeoutMs (compile error) |
| src/activities/ybx/YbxStatisticsActivity.{h,cpp} | needs YbxReadingData.cpp (link) |
| src/activities/plugins/ReadingStatsActivity.{h,cpp} | UITheme::getHeaderRect (compile error) |
| src/activities/plugins/PluginCatalogActivity.{h,cpp} | both-sides rework |
| src/activities/browser/OpdsBookBrowserActivity.{h,cpp} | both-sides rework |
| src/activities/home/HomeActivity.{h,cpp} | both-sides rework (consumes YbxHomeUi) |
| src/activities/home/FileBrowserActivity.{h,cpp} | both-sides rework |
| src/activities/util/DetailsActivity.{h,cpp} | both-sides rework |
| src/activities/ActivityManager.{h,cpp} | both-sides rework (goToPlugins signature differs) |

### components + src root (3)

| File | Reason |
|---|---|
| src/components/YbxHomeUi.{h,cpp} | calls ybx::loadReadingSummary (needs YbxReadingData.cpp) |
| src/main.cpp | both-sides rework |

### lib (5)

| File | Reason |
|---|---|
| lib/Txt/TxtToHtml.cpp | both-sides rework |
| lib/Epub/Epub/ParsedText.cpp, Section.cpp | both-sides rework |
| lib/Epub/Epub/blocks/TextBlock.{h,cpp} | both-sides rework |

### repo/docs/tests/platformio (13)

| File | Reason |
|---|---|
| platformio.ini | 2rc: version 2.0.0.1, XML_CONTEXT_BYTES=0; #23: patch_securenet pre-hook — reconcile |
| .github/workflows/ci.yml | 2rc CI differs |
| README.md, USER_GUIDE.md | 2rc-specific content |
| docs/{file-formats,plugin-events,sd-plugins,webserver-endpoints}.md | 2rc diverged |
| test/CMakeLists.txt, test/chapter_html_slim_parser/stubs/GfxRenderer.h, test/em_dash_line_breaking/CMakeLists.txt, test/release_json_parser/ReleaseJsonParserTest.cpp | 2rc test rework |

## Stage-1 verification

- Build after every port commit: **green** (`pio run -e default`, ESP32-C3).
- Compile failures encountered and resolved by reclassification to B (documented above): nativeapps/i18n (23 missing strings → fixed by union), `YbxReadingData.cpp` (pluginhttp timeoutMs), `ReadingStatsActivity.cpp` (UITheme::getHeaderRect), `YbxAccountActivity.cpp` (pluginhttp timeoutMs).
- Host tests were taken but **not executed** in stage 1 (CMake host tests; several reference #23-only sources such as ReaderSession — they belong to stage 2).
- freeink-sdk submodule pin left at 2rc's `f9cc1be`.

## Suggested stage-2 order

1. freeink-sdk pin decision (bump to `44427f8` or adapt) → unlocks the TLS/network cluster + `CatalogActivity` + `OtaUpdateActivity`.
2. `pluginhttp` signature reconcile → unlocks `PluginHttp.{h,cpp}`, `YbxReadingData`, `YbxHomeUi`, `YbxStatisticsActivity`, `YbxAccountActivity`.
3. ReaderSession API merge → `ReaderSession*`, `ReaderActivity`, `ReaderSessionJournal`.
4. Remaining both-sides rework (activities, lib, main.cpp, platformio.ini, ci.yml, docs) one subsystem at a time, each with a green build.
