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

## Remaining compile blockers (stages 3..N)

1. **ReaderSession API** — #23 changed the reader-session API (`noteTurn(succeeded)`, `summary()`, `pause()`, `finish()`, `ReaderSessionSummary`); 2rc's copy still uses the base API. Blocks the reader cluster and the `reader_session` test build.
2. **UITheme::getHeaderRect** — 2rc removed it (header-layout rework); #23 code calls it. Affects `EpubReaderMenuActivity`, `KOReaderSyncActivity`, `ReadingStatsActivity`.
3. **Both-sides rework** — file changed independently in 2rc and #23; needs a manual semantic merge.

## Class-B files remaining (stages 3..N)

### util (7)

| File | Reason |
|---|---|
| src/util/YbxReadingData.{cpp,h} | unlocked by `f7408d0c` (+ `PluginEventDeadline.h` already in tree) |
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
| src/activities/CatalogActivity.{h,cpp} | stage-1 "missing FontCacheManager" reason was wrong (firmware-side, in both trees); needs re-check |
| src/activities/settings/OtaUpdateActivity.{h,cpp} | tlsui::failureDetail / OtaUpdater #23 contract (TlsTransport now in tree) |
| src/activities/settings/AboutActivity.{h,cpp} | both-sides rework |
| src/activities/settings/FontDownloadActivity.cpp | downloadToFile call site updated in `5ded2299`; rest of the 2rc↔#23 delta pending |
| src/activities/ybx/YbxAccountActivity.{h,cpp} | unlocked by `f7408d0c` (pluginhttp #23 signatures) |
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

### lib (5 + unclassified EpdFont cluster)

| File | Reason |
|---|---|
| lib/Txt/TxtToHtml.cpp | both-sides rework |
| lib/Epub/Epub/ParsedText.cpp, Section.cpp | both-sides rework |
| lib/Epub/Epub/blocks/TextBlock.{h,cpp} | both-sides rework |

**lib/EpdFont cluster (~17 files, +3740/−3024: builtin font swap notosans→ubuntu, SdCardFont/SdCardFontManager/SdCardFontRegistry rework, `src/SdCardFontSystem.cpp`, `test/sd_card_font`) — never classified in stage 1.** Discovered during stage 2 via the CrossPointWebServer dependency analysis. Must be classified and ported before #23's `handleFontList` form can be restored.

### repo/docs/tests (12 + platformio.ini residual)

| File | Reason |
|---|---|
| platformio.ini | pre-hook added in `cd188837`; residual 2rc↔#23 deltas (version string, `XML_CONTEXT_BYTES`) intentionally kept at 2rc values |
| .github/workflows/ci.yml | 2rc CI differs |
| README.md, USER_GUIDE.md | 2rc-specific content |
| docs/{file-formats,plugin-events,sd-plugins,webserver-endpoints}.md | 2rc diverged |
| test/CMakeLists.txt, test/chapter_html_slim_parser/stubs/GfxRenderer.h, test/em_dash_line_breaking/CMakeLists.txt, test/release_json_parser/ReleaseJsonParserTest.cpp | 2rc test rework |

## Verification

- **Firmware**: `pio run -e default` green after every stage-1 and stage-2 commit.
- **Host unit tests** (`pio run -e default -t unit-tests` → cmake/ctest under build/test): **830 passed / 844** (13 failed, 1 not built). Every failure is attributable to still-unported class-B sources — verified by `git diff cd188837..HEAD --name-only` showing none of the failing areas were touched by stage-2 commits:
  - `ReaderSessionTest_NOT_BUILT` — the binary does not compile against 2rc's ReaderSession API (#23 API pending stage 3);
  - 11 cases in `TxtToHtmlTest` / `MarkdownToHtmlTest` — 2rc's `lib/Txt/TxtToHtml.cpp` still in tree;
  - `clipping_selection_text`, `recursive_dictionary` — harnesses scraping `src/activities/reader/*` (stage 3).
- Host tests were taken in stage 1 but first executed in stage 2.

## Suggested stage-3 order

1. ReaderSession API merge → `ReaderSession.{h,cpp}`, `ReaderActivity`, `ReaderSessionJournal`, reader activities, `reader_session` tests.
2. lib/EpdFont cluster (classify + port) → restores #23's `handleFontList` form, unblocks the font tests.
3. pluginhttp consumers, unlocked by `f7408d0c`: `YbxReadingData` → `YbxHomeUi`, `YbxStatisticsActivity`, `YbxAccountActivity`.
4. Remaining both-sides rework (`PluginEvents`, `FirmwareIdentity`, activities, lib/Txt+Epub, `main.cpp`, ci.yml, docs, tests) one subsystem at a time, each with a green build.
