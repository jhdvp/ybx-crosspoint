# Upstream sync 2026-11-12

Two merge commits bring this fork up to upstream `2.0.0` and then to
`upstream/develop` (2026-11-11). Each conflict was resolved file by file; the
table records what the conflict was and which side won.

Fork rules that apply everywhere: keep upstream behaviour where YBX changed
nothing, keep the YBX feature where it is intentional, and merge both where both
sides changed. YBX plugin, sync, account, QR, clippings, notes and statistics
code is never dropped.

## Merge 1 — upstream 2.0.0 (60 commits over 2.0.0rc)

23 unmerged paths (22 content conflicts + the `freeink-sdk` gitlink).

| File | Conflict | Decision |
| --- | --- | --- |
| `.github/workflows/ci.yml` | Host-test job installed different apt packages on each side. | Union: keep upstream's `cmake ninja-build python3-pil zlib1g-dev`, which already covers the fork's Pillow need. |
| `docs/file-formats.md` | Upstream documented section cache versions 65..73; the fork documented its Markdown/TXT HTML cache versions in the same spot. | Take upstream's section version list and keep the fork note as its own subsection under `## section.bin`. |
| `docs/plugin-events.md` | Both sides rewrote the `reader.exit` row and the `{event.id}` row. | Upstream's variable list (`book, percent, end_percent`) plus the fork's `reader.session` wording and extended `{event.id}` documentation. |
| `docs/sd-plugins.md` | Capability table, `{list_id}` variables, pagination rules, folder/bundle bullets. | Upstream's new rows (`list_index_mapping`, `list_requirements`, `server_paths`, `{list_id}`, `{list_id_enc}`, "rows without a title are not shown") plus the fork's `stats.native.v1`, `ui.dashboard.v1`, `require_url`, dynamic-list and server-path bullets. |
| `docs/webserver-endpoints.md` | `pluginCapabilities` example list. | Union of both capability sets (13 entries), matching `FirmwareIdentity.h`. |
| `freeink-sdk` (gitlink) | Fork pinned `44427f8d`, 2.0.0 pins `5e94fe74` (59 SDK commits apart). | Keep the fork's `44427f8d`. The reviewed SecureNet patch series (`scripts/patch_securenet.py`) is hash-guarded to that revision: at `5e94fe74` stage 0001 no longer applies and the new SDK still lacks host-name verification, `WOLFSSL_VERIFY_PEER` on a CA bundle, `ConnectFailure`, redirect credential scoping and `DeadlineDns` — all of which `src/network/TlsPolicy.cpp` consumes. `YBX_FORK.md` requires a pin advance in its own commit, so the pin and the series are re-reviewed together by a human, not inside a sync merge. |
| `lib/Epub/Epub/Section.cpp` | Both bumped `SECTION_FILE_VERSION`; the fork carried the RTL focus-split fix as v72, upstream shipped the same fix as v71. | Take upstream's chain: `SECTION_FILE_VERSION = 73`. The fork's v72 bump is redundant because the fix itself is upstream now. |
| `lib/I18n/translations/english.yaml` | Fork added `STR_PLUGIN_UPDATE_AVAILABLE` and the YBX/stats block; upstream added new preview/font/TLS keys and deleted four unused spacing keys. | Keep every fork key, adopt upstream's new keys, drop the four upstream-deleted keys, and keep the file in upstream's key order. |
| `lib/I18n/translations/russian.yaml` | Same regions as English, plus two OPDS strings defined by both sides. | Keep the fork's wording where it is a YBX string (`STR_DOWNLOAD_COMPLETE`), adopt upstream's wording for shared keys, delete the fork's duplicate `STR_OPDS_NOT_A_BOOK`/`STR_OPDS_UNAVAILABLE`, drop the four deleted keys. |
| `src/activities/boot_sleep/SleepActivity.cpp` | Both wrapped the sleep overlay in a `BwFrameKeeper` and restructured the same functions. | Take upstream's structure; the fork's four-tone `YbxSleepLogo4` companion mark is untouched above it. |
| `src/activities/plugins/PluginCatalogActivity.cpp` | 23 hunks: list-index loading, paging windows, long-press, manifest fields, download path. | Upstream's list-index mapping (`listsItemsPath`/`lists*Path`, `{list_id}`, `resolveServerPath`, `fixedListCount`, status-returning `loadBrowseListIndex`) and unified `countsTowardPage` paging; fork features kept: verified-TLS reporting (`failTlsIfAny`), `lists.auth` empty-list handling, `parent` folders (`listsParentPath`), on-reader shelf, update cache/target pages, built-in YBX catalog, EPUB magic check, `wantsTouchLongPress` on `BROWSING`. |
| `src/activities/plugins/PluginCatalogActivity.h` | `BrowseList` fields, list-index config members, navigation state, `loadBrowseListIndex` signature. | Upstream's config members and signature plus the fork's `parent`/`listsParentPath`, group-trail and folder-label state; the fork's `ListIndexResult` enum is replaced by upstream's `int& status`. |
| `src/activities/plugins/PluginCatalogPaging.h` | add/add: both created the helper; upstream adds `countsTowardPage`. | Take upstream's file (superset; the rest was byte-identical). |
| `src/activities/reader/EpubReaderActivity.cpp` | `launchKOReaderSync`: fork added RenderLock + kosync refresh; upstream added a named argument. | Merge both: fork lock and `ybxkosync::refresh()` with upstream's `/*pluginEventsOnly=*/true`. |
| `src/activities/reader/KOReaderSyncActivity.cpp` | Include block. | Keep the fork's `YbxDocumentIdentity.h` / `YbxKosync.h` includes. |
| `src/main.cpp` | Battery logging branch. | Keep the fork's branch (upstream only reformatted the same code). |
| `src/network/CrossPointWebServer.cpp` | Header collection for the captive-portal Host, and the fetch handler rewrite. | Upstream's 8-header list and its rewritten fetch handler (`errorBody`, `resumable`, `configureClient`) with the fork's TLS posture injected: `setUserAgent("YBX")`, `tlspolicy::installTrust`, `clearFailure`, `ensureClockFor`. |
| `src/util/FirmwareIdentity.h` | Capability string. | Union: upstream's `list_index_mapping`, `list_requirements`, `server_paths` plus fork's `lists.auth`, `folders.json`, `stats.native.v1`, `ui.dashboard.v1`. |
| `src/util/PluginEvents.cpp` | Queue flag. | Keep the fork's `bool queued = true;` with upstream's variable-list changes. |
| `src/util/PluginEvents.h` | `reader.session` comment and `reader.exit` variables. | Fork comment, upstream variable list. |
| `src/util/PluginHttp.cpp` | `openClient` signature and TLS setup. | Keep the fork: `shouldAbort`/`timeoutMs` arguments, "YBX" user agent, `tlspolicy::prepareHttpsClient`, `recordFailure`. |
| `test/CMakeLists.txt` | Both appended `add_subdirectory` blocks; upstream repeated one the fork already had. | Keep the fork's superset list, drop the duplicate line. |
| `test/plugin_catalog_paging/PluginCatalogPagingTest.cpp` | add/add: upstream adds hidden-row paging tests. | Take upstream's file (superset of the fork's tests). |

## Merge 2 — upstream/develop

(Recorded after the second merge commit; see the section appended below.)

## Checks run for this sync

- Host unit tests: `cmake -S test -B build/test -G Ninja` +
  `ctest --test-dir build/test` — 1282 of 1282 pass.
- Firmware build: `pio run -e default` — SUCCESS with the merged tree and the
  fork's SDK pin.
- Not checked here: `bin/clang-format-fix` (no clang-format on this host) and
  the SDK `test/host/run.sh` harness (unchanged pin, so unchanged series).
- Hardware behaviour (TLS trust, QR pairing, clippings sync, shelf, sleep art)
  is still the owner's validation, not an agent's.

## Open items for the owner

1. Advance `freeink-sdk` to the revision each upstream release pins and re-port
   the SecureNet series, following `docs/ybx-sdk-patches.md` §"Updating the SDK
   pin". Until then the fork builds upstream 2.0.0 firmware against an SDK 59
   commits behind 2.0.0's own pin, so board fixes that live in the SDK (Picco
   power saving and frontlight, eego A4 timing, SDMMC 40 MHz, GT911 touch,
   `.notdef` glyph loading, bounded JSON previews) are not in this build.
2. Re-check the fork's `catalog.list_index_mapping` support against the YBX
   `parent` field: the merge extended it with `listsParentPath`, which is
   documented in `docs/sd-plugins.md` only as a fork addition.
