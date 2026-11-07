# YBX unified → 2.0.0rc port map (stages 1–2)

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

`pio run -e default` green after **every** commit in both stages.

## Stage 1 — mechanical takes (206 of 281 files, commits `a4f057ea`..`a532ff98`)

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
| **Total** | **206** | **75** | 10 commits |

### Stage-1 special cases

- **i18n union (`f413ad13`)** — #23's yaml deltas are purely additive, but 2rc diverged from the base (english −22/+4: deleted old `STR_YBX*`/`STR_PLUGIN_UNINSTALL*`/`STR_SPACING_*` keys, added `STR_NOT_IN_PREVIEW`, `STR_TIGHT`, `STR_WIDE`, `STR_EXTRA_WIDE`). Union = #23's yaml + the four 2rc-only keys re-appended per language. Nativeapps' 23 missing `STR_YBX_*` strings resolved.
- **Transitive deps not in the 281 diff** (unchanged by #23, absent in 2rc): `src/util/PluginEventDeadline.h` (needed by `YbxReadingData.cpp`) and `sd-card/plugins/ybx/config.json` (plugin sleep-cover size 480×800). Both already taken in stage 1 (`fc866df9`, `22279555`) — the original "kept for stage 2" note was stale.

## Stage 2 — SDK pin + network / clippings / PluginHttp (5 commits)

| Commit | Content |
|---|---|
| `cd188837` `port(sdk)` | freeink-sdk pin `f9cc1be` → `44427f8`; `platformio.ini` gains exactly one line, `pre:scripts/patch_securenet.py` (all other 2rc config kept verbatim: version, `XML_CONTEXT_BYTES=1024`, other hooks) |
| `5ded2299` `port(network)` | `HttpDownloader.{h,cpp}`, `OpdsHttpTransport.{h,cpp}`, `LcpDevice.{h,cpp}`, `OtaUpdater.cpp`, `TlsPolicy.cpp`, `TlsRoots.h`, `TlsTransport.h` + call-site fix in `FontDownloadActivity.cpp` (`downloadToFile`: drop `downgradeRedirectsToHttp`, comment now explains verified-https via the CA bundle) |
| `ed306bb7` `port(clippings)` | `ClippingSync.cpp`: optional quote-context sync (`syncNativeNoteAnchors`, 404-tolerant), `request(..., int* returnedStatus)`, `tlspolicy::prepareHttpsClient` replaces `setInsecure()` |
| `f7408d0c` `port(util)` | `PluginHttp.{h,cpp}` taken #23-verbatim (reconciliation decision below) |
| `bde0eacc` `port(web)` | `CrossPointWebServer.{h,cpp}` (#23 with one 2rc hunk, below) + new `lib/FsHelpers/HiddenItems.h` (a #23 file missed by stage-1 classification) |

### What the SDK bump actually delivers (corrections to the stage-1 blockers list)

Verified by content, not by the stage-1 notes:

- `SecureClient::ConnectFailure`/`connectFailure()` and `http.connect(shouldAbort)` do **not** come from the pin bump — they are added by the `scripts/patch_securenet.py` patch series (0001-deadline-dns-tcp, 0002-tls-verify), now enabled as a pre-hook. The pin bump alone would not compile #23's network code; the pre-hook is the load-bearing part of `cd188837`.
- `FontCacheManager` is firmware-side (`lib/GfxRenderer`), present in both trees — it was never an SDK-pin blocker. The stage-1 claim re `CatalogActivity` is withdrawn; `CatalogActivity` re-checks in stage 3.
- `TlsPolicy.cpp`, `TlsRoots.h`, `TlsTransport.h` are **new files absent in 2.0.0rc** (pure takes), not 2rc files blocked by the SDK.
- `OtaUpdater.h` and `OtaReleasePolicy.h` verified byte-identical in both trees — nothing to port; `TLS_ERROR`/`UNTRUSTED_SOURCE_ERROR` were already in 2rc.

### pluginhttp reconciliation (decision)

2rc removed `timeoutMs` from `request` (which keeps `shouldAbort`) and both `shouldAbort`/`timeoutMs` from `requestToFile`/`mintPasswordToken`; #23 re-adds them as **trailing defaulted parameters** (`timeoutMs = 30000` equals 2rc's hardcoded `setTimeout(30000)`). Decision: take #23-verbatim (`f7408d0c`) — the #23 signatures are a source-compatible superset. All 9 in-tree call sites surveyed (`PluginEvents.cpp` ×3, `CrossPointWebServer.cpp` ×1, `PluginCatalogActivity.cpp` ×5): all fit within 2rc arity. Stage-3 #23 files (`YbxReadingData`, `YbxAccountActivity`) then compile as written, no shims.

### CrossPointWebServer merge (decision)

Took #23 wholesale after hunk-by-hunk review of the 2rc↔#23 delta: staged upload overwrite (`.part` + `HalStorage::replaceFile`, library-cache invalidation on failed replace), plugin-file MD5 ETag + `If-None-Match` 304, TLS-verified `handleFetch` (actionable 502 bodies: `tls clock` / `tls hostname` / `insecure redirect` / `tls certificate`), dot-file deletion allowed (only sensitive paths protected), `UploadState.staged`. **One hunk kept in 2rc form**: `handleFontList` still uses 2rc's `SdCardFontSystem` API (`file.path` directly) because the lib/EpdFont cluster is deferred; switch to #23's `family.filePath(...)` form when that cluster is ported.

### PluginCatalogActivity merge (decision)

Three-way `git merge-file` (2rc/base/#23) left 24 conflict regions (21 cpp + 3 h); **every conflict resolved to the #23 side** — each was the feature delta (update-target flow, shelf popup, uninstall via long-press, folder history through `catalogfolders` helpers, `ListIndexResult` token-refresh on the browse-list index, dashboard rows). Auto-merge correctly kept 2rc's no-paging structure where #23 changed nothing; #23's `PluginCatalogPaging.h` and `browsePaging()` were **not** re-added (2rc lineage never had paging in this file; the merged file does not reference it). The known auto-merge trap (regions where base==#23 but 2rc differed resolve to 2rc) silently dropped declarations #23 code needs; restored manually: `ListIndexResult` enum + method (with the #23-verbatim `loadBrowseListIndex` body), `onRowLongPress`/`handleButtons` overrides, `confirmUninstallPlugin` declaration, `PluginRef.dir` + `ref.dir = e.dir` fill (matches HEAD's argument-free `PluginLocations::scanPlugins()`), `folderLabelHistory`, `visibleListCount/indexAt` API. To answer "did 2rc remove uninstall?": no — 2rc's independent lineage never contained it (no removal commits, zero `uninstall` matches in 2rc `src/`), so this port adds #23 behaviour without reverting 2rc work. Supporting base-class machinery ported additively into `CatalogActivity.{h,cpp}` (byte-identical to #23 apart from these): `failTlsIfAny()` (uses `tlsui::failureMessage(tlspolicy::lastFailure())`, both already in tree) and the `downloadFileError` override channel (reset in `beginDownload`, honored in `finishDownload` for `FILE_ERROR`). `DetailsSpec::hasPrimaryAction` ported the same way into `DetailsActivity` (routing + render guards only; the heading-marker/pageProps deltas stay queued for the DetailsActivity re-check).

## Stage 3 — Activities, Components, Lib, Main, Tests, Build (16 commits)

| Commit | Subsystem | Description |
|---|---|---|
| `e724c23d` | util | Plugin event outbox upgrade and crash-resilient reader session journal (`ReaderSessionJournal.{h,cpp}`, `PluginEvents.{h,cpp}`) |
| `066adddb` | lib | Markdown cache v2 + RTL focus split (`TxtToHtml.cpp`, `ParsedText.cpp`, `Section.cpp`, `TextBlock.{h,cpp}`) |
| `19544c51` | reader | Reader session journal integration, KOReader progress sync, reader menu updates (`ReaderSession.{h,cpp}`, `ReaderActivity.{h,cpp}`, `EpubReaderActivity.{h,cpp}`, `EpubReaderMenuActivity.cpp`, `KOReaderSyncActivity.cpp`) |
| `eb915513` | test | Register stage-3 suites in CMakeLists.txt and fix dictionary harness |
| `a68ac1fa` | ybx | Reading summary data layer and home UI panel with 2rc header idiom (`YbxReadingData.{h,cpp}`, `YbxHomeUi.{h,cpp}`) |
| `fd91391f` | ybx | YBX account and statistics activities (`YbxAccountActivity.{h,cpp}`, `YbxStatisticsActivity.{h,cpp}`) |
| `3a12e907` | plugins | Reading stats dashboard activity with 2rc header idiom (`ReadingStatsActivity.{h,cpp}`) |
| `a571ac62` | plugins | Plugin catalog features merged onto 2rc base (`CatalogActivity.{h,cpp}`, `PluginCatalogActivity.{h,cpp}`, `DetailsActivity.{h,cpp}`) |
| `8f4cacfc` | browser | OPDS catalog navigation history and bounded fetch (`OpdsBookBrowserActivity.{h,cpp}`) |
| `54cfd0a4` | util | Add plugin capabilities in `FirmwareIdentity.h` (`catalog.encoded_item_fields`, `catalog.folders.json`, `stats.native.v1`, `ui.dashboard.v1`) |
| `891017b9` | settings | Position sync in About, FontDownload verified HTTPS & TLS detail, OtaUpdate Retry/Back controls & wrapped errors |
| `ff685a87` | home | YBX Home UI integration, plugin update toast, on-reader shelf with lockedRoot, ActivityManager routing |
| `0faf0ddb` | main | Isolated recovery boot on SD failure (`NoSdRecoveryActivity`), crash session journal recovery, YBX KOReader sync begin |
| `c6934839` | test | Add YBX and factory image OTA release parser tests (`ReleaseJsonParserTest.cpp`) |
| `9bc8c628` | build | Bump version to 2.0.0.1 and set `XML_CONTEXT_BYTES=0` in `platformio.ini` |
| `605ec7c4` | docs | Update `docs/{plugin-events,sd-plugins,webserver-endpoints}.md` |

## Verification

- **Firmware (`pio run -e default`)**: GREEN after every single commit. Flash: 87.9%, RAM: 21.3%. Full image and factory binary generated.
- **Host unit tests (`ctest --test-dir build/test`)**: **1041 passed / 1041 (100%)**, 0 failures.

## Decisions and adaptations summary

1. **2.0.0rc Architecture and UI Framework Kept**:
   - Headers and menus use 2.0.0rc's header layout (`UITheme::getHeaderHeight`, standard 2rc chrome) rather than older `UITheme::getHeaderRect`.
   - Font architecture: retained 2.0.0rc's Noto Sans fonts, registry and cache system.
   - Reader session API: adopted #23's `ReaderSession` contract with crash recovery journal while adapting call sites in 2rc activities.
   - Catalogs & Browser: merged bounded-window cancellation, selection restore, and TLS error diagnostics into 2rc list activities.
   - Home UI: conditional `YbxHomeUi` for X3/X4/X4Pro with fallback to 2rc's `CoverGridHomeUi` / classic home.
   - SD Error Recovery: graceful fallback to `NoSdRecoveryActivity` in an isolated boot loop without panicking.
   - Build flags: `version = 2.0.0.1`, `XML_CONTEXT_BYTES=0` (Expat memory reduction for C3), `pre:scripts/patch_securenet.py`.
