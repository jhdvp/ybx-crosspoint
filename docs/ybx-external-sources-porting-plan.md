# YBX Reader — consolidated external-source reuse and change specification

**Audit date:** 2026-11-02
**Scope:** 19 reviewed public repositories (18 initial candidates + InkHome); Xteink X3/X4 (ESP32-C3) and X4 Pro (ESP32-S3), YBX Server/Desktop/PocketBook where necessary.
**Delivery policy:** research and work packages only; no upstream code is merged by this document. Release/production unchanged.

## 0. Baselines, rules, and interpretation

- Published development baseline inspected: jhdvp/ybx-crosspoint, origin/feat/ybx-native-integration at 20c5d6e4 (2026-11-01); **NOT** equivalent to the larger local integration candidate integrate/ybx-all-firmware-20261029 at 979386ee. The latter includes additional native statistics/awards/settings/account work. InkHome and several other forks are based on earlier CrossPoint 1.6.x, while this candidate targets 2.0.0-rc; API signatures, HAL and data layouts must be adapted, not wholesale merged. Reconcile branches and re-run the inventory before starting feature ports. Do not mistake a worktree or Kaneo "in progress" for merged/released code.
- Supporting system: jhdvp/ybx-esp32-emulator at 30567c9 (2026-11-02), already incorporating eink-qemu 0.9.2 / QEMU 11.1.2. Never schedule a redundant QEMU-version rebase; improve acceptance scenarios and missing peripheral behaviour.
- Existing YBX assets to preserve: ReaderSession + session journal; bounded event queue and sleep image delivery; TLS/host verification candidates; KOSync; plugin hub and YBX catalog auth; native home, account, stats, achievements candidates; X4 Pro HAL; OTA/recovery policies; cross-client server-owned reading state.
- Canonical authority: YBX Server for account/awards/goals/provider linking; firmware for measured local activity and pending offline events; a single session identity for retry. vCodex local JSON and InkHome JSON are **import formats**, not competing live truth.
- Remote plugins are declarative/data integrations. InkHome's Native Apps are **compiled C++**, not installable executable plugins. Never allow an SD package to execute arbitrary code.
- Hardware: host unit tests, simulator rendering, emulator boot tests and successful PlatformIO builds are separate gates. No physical X3/X4/X4 Pro acceptance was confirmed in this audit.
- Profiles must obey actual board flash partitions and maximum contiguous heap, not just total RAM. X4 Pro's PSRAM must never hide a regression on C3. No feature should change the ordinary page-turn, sleep, Wi-Fi and security paths unless required.
- Exclude all credentials from samples, logs and Kaneo. Redact URLs with access tokens, titles of private books, device identity, auth headers and cookies.

### Existing Kaneo tasks / do not duplicate

Primary board: YBX Reader · прошивка, project m7bzuab46as9kqwxtwlqw38i in workspace kmuPv6G5D6zt1dhwTeYFuf6E7HPLmaJ2.
- #2 vCodex import; #3 session delivery; #5 sleep/event FIFO; #8 native stats; #11 TLS; #12 OPDS timeout; #13 sleep-screen acceptance; #14 release/hardware; #15 upstream integration; #18 achievements; #19 client parity; #22 device library; #23 settings; #24 X4 Pro; #25 SDK DNS/TCP; #26–38 QEMU; #39 Rate Cloud acceptance.
- Rate Cloud sprint already has atomic settings/migrations, token privacy, resource budgets, release QA, sync dedup, OTA rollback, EPUB memory tests, offline UX, test infrastructure and malformed-response cases. Add cross-reference to those cards, not clones.
- Priority prerequisites: resolve #5/#11/#25 and review/reconcile #15/#39 BEFORE a public device build; never change status to Done solely because this review exists.

## 1. Repository-by-repository source register

Snapshot commits are intentionally pinned: update individual rows and inspect subsequent changes before cherry-picking anything. "Reuse" means selective port or adaptation; "study" is not authorization to copy. Links go to upstream GitHub.

| # | Repository @ inspected commit | What the code actually provides / entry points | Disposition |
|---|---|---|---|
| 1 | [osteotek/instapaper.crosspoint-plugin](https://github.com/osteotek/instapaper.crosspoint-plugin) @ 6d08af0238da | worker/events.js: reader.exit + sleep.enter retry/error policy; worker/index.js: article HTML/images to EPUB; src/plugin.js: catalog/SD delivery | Reuse event-handling *semantics*, plan Instapaper connector on server. Do not copy external credentials into device logs. MIT. |
| 2 | [StreamlinedStartup/crosspoint-reader](https://github.com/StreamlinedStartup/crosspoint-reader) @ 7d0941975a97 | reTerminal E1001/ESP32-S3 port, HAL/display/key/flash configuration | Hardware reference only. Not a core Xteink feature; do not merge its CrossPoint base. MIT. |
| 3 | [datojulien/crosspoint-cwa-bridge](https://github.com/datojulien/crosspoint-cwa-bridge) @ 63bcc5eb66c4 | src/crosspoint_cwa_bridge/optimizer.py, feeds.py, cache.py; OPDS profile feeds; protected EPUB conversion and content-validated derivatives | Extract independent specification/test cases for server delivery. GPL-3.0: license review before any copying; prefer separate original implementation. |
| 4 | [nandanito/crossmono](https://github.com/nandanito/crossmono) @ 99650acd48f1 | lib/BmpToBmpConverter/*, test/bmp_to_bmp/*; test/mono/test_size_gate.py and partition checks; various upstream-like fixes | Compare bounded image conversion and CI flash gates, only port proven missing deltas. MIT. |
| 5 | [flyboy-byte/crosslight](https://github.com/flyboy-byte/crosslight) @ a313bf3ab24d | src/utilities/UtilityRegistry.cpp, AI/utility modules, BLE/Wi-Fi components | Borrow architecture principles for registered built-ins only. Exclude offensive Wi-Fi/BLE features, credential harvesting, cloud-key-in-firmware patterns and unrelated Bible tools. MIT. |
| 6 | [QrCommunication/library-manager](https://github.com/QrCommunication/library-manager) @ 2ce328bcff7e | crates/library-core/src/optimizer.rs, calibre_wireless.rs, transfer.rs: variants, bounded transfers, checksums, device catalog | Desktop/server transport reference; not a firmware-embedded Rust rewrite. GPL-3.0: independent implementation or separate compatible service, after licensing assessment. |
| 7 | [filipkowicz/lost-xte-tag](https://github.com/filipkowicz/lost-xte-tag) @ 3cd5c577d163 | app.js: browser-only local QR/vCard and owner strip generator sized X3/X4 | Optional privacy-aware YBX owner overlay or web helper. No mandatory account personal info on sleep by default. MIT. |
| 8 | [chippwalters/crosspoint-reader / PaperBit](https://github.com/chippwalters/crosspoint-reader) @ c26c82e8f52b | lib/Epub/Epub/markdown/MarkdownSection.cpp, MarkdownToBoxParser.cpp; src/network/SerialFileTransfer.cpp; src/ble/BleKeyboardManager.cpp; src/activities/notes/*; src/crypto/VaultCrypto.cpp | Selectively port Markdown, *optional* BLE writer, USB recovery file transport. Do not adopt insecure TLS bypass or broad base fork. MIT; check vendored notices. |
| 9 | [readloopsync/readloop](https://github.com/readloopsync/readloop) @ cf89fa1dd840 | OPDS + Readwise pipeline, proposals/crosspoint-sync/readloop-route.ts and readwise-reader.ts; percentage seeding before first KOSync pull | Protocol/design reference. Proposal endpoints are not a proven production contract; user-scoped access and ID/hash mapping needed. MIT. |
| 10 | [sypianski/hairline.koplugin](https://github.com/sypianski/hairline.koplugin) @ 38211244655a | KOReader Lua plugin patching ReaderFooter, bottom-edge hairline progress widget | Visual concept only; not a CrossPoint plugin. AGPL-3.0: do not lift Lua code into MIT firmware. |
| 11 | [jordiboehme/epub-tailor](https://github.com/jordiboehme/epub-tailor) @ 483ae42f3394 | crates/core/profiles/x3.json, x4.json; src/image/encode.rs baseline JPEG and byte budgets; src/html/tables.rs; EPUB/Markdown validation pipeline | Primary preferred MIT conversion implementation/reference. Add tested X4 Pro profile separately; execute server-side or at ingestion, never add heavy Rust to ESP32. |
| 12 | [klanva/BookPoint](https://github.com/klanva/BookPoint) @ d4df14ecdaa8 | src/util/FootnoteTextExtractor.cpp, PowerStats.cpp; src/stats/StatsStore.cpp; src/activities/network/UsbDriveActivity.cpp; X4 Pro diagnostics | Use footnote/power/RTC UX deltas only after comparing upstream. YBX already has USB Drive; do not duplicate. Never bypass ESP-IDF OTA safety without dedicated security review. MIT. |
| 13 | [IS7V4N/ESP32_E-Reader](https://github.com/IS7V4N/ESP32_E-Reader) @ ef1aeb39f0b6 | custom ESP32-C3 board, 4MB partition table, keypad word lookup and dictionary, PWM frontlight/low-power states | Hardware/compactness benchmark. Dictionary already exists upstream; only unique fast button navigation or size methods are candidates. Firmware subtree has a separate license. |
| 14 | [cailinpitt/newspaper](https://github.com/cailinpitt/newspaper) @ 169f78ef3af4 | src/epub_builder.py RSS→EPUB issue, src/device.py /api/status poll and /upload lifecycle; old issue cleanup | Server job/OPDS shelf inspiration; never leave reader Wi-Fi awake just for automated push. No clear root LICENSE found: do not copy code without verification. |
| 15 | [ed-fruty/crosslingua-reader](https://github.com/ed-fruty/crosslingua-reader) @ a0a66d553ff5 | src/modules/lingua/modes/* (paired, interlinear, tooltip, translation-only), engines/ParagraphTranslator.cpp, services/HeapBackpressure.cpp, src/components/CoverGrid.cpp | Optional bilingual reading; stage server-produced bilingual EPUB before on-device online translation, especially C3. Respect user consent/privacy and memory budgets. MIT. |
| 16 | [serialx/eink-qemu](https://github.com/serialx/eink-qemu) @ d1b24a5fbc1b | eink-qemu/cli, scripted boot/input/screenshots/heap, C3/S3 display/SD/touch/peripherals; QEMU 11.1.2-based | ALREADY integrated as reference 0.9.2 in YBX emulator: extend test scenarios/peripheral fixes, no redundant version migration. QEMU/mixed licenses: maintain notices. |
| 17 | [tvhdc/crossvi](https://github.com/tvhdc/crossvi) @ b16d0c016ac9 | lib/Serialization/AtomicFile.cpp and StagedFileTransaction.cpp; lib/BootRecovery/BootRecoveryPolicy.cpp; lib/Memory/MemoryBudget.cpp; src/activities/reader/VCodexStatsImporter.cpp and tests; stats completion transaction | Highest-value crash recovery and import reference. Audit current YBX journal/queue first; port only missing guarantees. MIT. |
| 18 | [Sichroteph/YACP](https://github.com/Sichroteph/YACP) @ 23568ad35807 | lib/Memory/Arena.h, ScratchWorkspace.cpp; src/activities/reader/ReaderProgressSaveDebouncer.h; quick resume/SD write removal, autonomy metrics, image sleep conversions, StarDict | Highest-value performance source. Copy isolated strategy with host+device-specific measurements; power latch and waveforms require real hardware. MIT. |
| 19 | [mohitagw15856/inkhome](https://github.com/mohitagw15856/inkhome) @ 735e74d73b95 | src/apps/AppRegistry.*, AppsActivity.h, inkhome/NoSdCardActivity.cpp and SlotGuard.cpp, apps/fwswitch/FirmwareSwitchApp.cpp, docs/flash/flasher.js; Inkcards SM-2, InkWriter, PocketWiki, Notes, stats tracker | Core Native Apps registry, no-SD recovery and opt-in modules; YBX server owns achievements and sessions. Non-device-tested and nearly fills C3 app slot. MIT. |

### Evidence and licence boundaries

- InkHome host C++ tests: 67/67 passed on 2026-11-02; its web-flasher tests: 8 passed, 2 skipped (hardware fixture cases). **Not** firmware/hardware acceptance. InkHome reports release image 6,196,237 / 6,553,600 bytes (~94.5% flash); its on-device Wi-Fi after BLE remains unverified.
- The YBX integration candidate's recorded test/build results are historical and commit-specific (see docs/ybx-validation-status.md, docs/ybx-hardware-acceptance.md on integration branch). Re-run for the actual combined candidate; no assumption of those tests still passing on current head.
- CWA Bridge and Library Manager GPL-3.0, Hairline AGPL-3.0; Newspaper root licence unspecified. QEMU has mixed licences; hardware designs may have separate licences. All other source-level ports require file-level notices and dependencies inspection, even when root is MIT. No blanket "all files MIT" declaration.
- Architecture/method patterns may be independently reimplemented. Do not paste copyleft code into a differently licensed component without establishing compliance and compatible distribution terms.
- Direct firmware integration must cite precise upstream commit/file in the PR and add compatible attribution/notice.

## 2. Decision-driven work packages (the actual change backlog)

Each package is an atomic PR or small ordered PR series. Priority: P0 release blocker, P1 essential, P2 valuable feature, P3 optional. "Done" requires the evidence stated, not source presence.

### P0-A — Reconcile current firmware candidates BEFORE porting

**Sources:** existing YBX repo and Kaneo #15, #39.
**Files/areas:** docs/ybx-fork-inventory.md, docs/ybx-client-parity.md, docs/ybx-hardware-acceptance.md, scripts/ybx_upstream_sync.py, SDK pin and worktree merge graph.
**Change:** reconcile origin/feat/ybx-native-integration with integrate/ybx-all-firmware-20261029 and accepted Rate Cloud changes. Establish one review candidate, inventory every changed upstream file and record exact SDK/plugin versions. Avoid merging competing unreviewed agent work and avoid any production release.
**Acceptance:** clean reproducible test worktree; branch/commit/SDK manifest; host and all claimed board build targets; unresolved conflicts recorded; Kaneo statuses remain honest. **Gate** to new features.

### P0-B — Preserve TLS, network deadlines and sleep events

**Sources:** Instapaper worker/events.js (error classification); CrossVi durable transactions; YACP sleep timing.
**Existing:** Kaneo #3, #5, #11, #12, #25.
**Change:** verify HTTPS certificate chain/hostname/time and redirect boundaries; bound DNS/TCP/JSON; sleep image delivery must not starve behind permanent nonessential events; enforce idempotent retry for reader.session, reader.exit and sleep.enter. Explicitly distinguish permanent malformed events from temporary 401/429/5xx failures. Never "return 200" indiscriminately for errors that require token repair.
**Acceptance:** host tests for malformed, auth-expired, 429, 500, infinite DNS, slow response, no Wi-Fi, backlogged FIFO, sleep deadline; targeted QEMU network tests; no private log leakage. **Gate** to release.

### P1-C — Crash-safe SD persistence and boot recovery

**Sources:** CrossVi AtomicFile.cpp, StagedFileTransaction.cpp, BootRecoveryPolicy.cpp; InkHome NoSdCardActivity.cpp, SlotGuard.cpp.
**Targets:** one shared transactional writer for settings, achievements/history cache, download state, bookmarks, notes and reader journal. *Do not replace* already-tested PluginQueueStorage and ReaderSessionJournal transactions without explicit proof of improvement.
**Change:** temporary complete write + validate + flush/close + rename/backup rotation + recovery on next boot; distinguish absent vs corrupt vs future-schema files; preserve source bytes on migration; no-SD limited recovery screen with no unguarded filesystem access. Separate boot stage skip policy under bounded repeated crash, without silently disabling security.
**Acceptance:** inject power failure before/after write/close/rename, full SD, short write, wrong schema, backwards/forwards migration, unreadable card, missing SD; old or new **valid** copy remains; recovery UI navigable without card. Existing sprint task #1 is relevant.
**Risk:** high; FAT atomicity, file close and partition distinctions must be verified per board.

### P1-D — Memory footprint, fast resume and SD write budgets

**Sources:** YACP Arena.h, ScratchWorkspace.cpp, ReaderProgressSaveDebouncer.h; CrossVi MemoryBudget.cpp; CrossLingua HeapBackpressure.cpp; CrossMono test_size_gate.py.
**Change:** measure largest contiguous free heap, low-water mark, stack margins, allocation failures and flash bytes in cold boot, EPUB chapter load, page turn, OPDS, Wi-Fi, KOSync, sleep, resume. Replace proven fragmentation hotspots with task-owned arena/scratch where safe. Avoid redundant SD updates at wake; delay catalogue/stats/footnote loads until screen requires. Debounce *local position writes*, but never lose final position at exit/sleep/crash or alter server event cadence.
**Acceptance:** same fixture/input on baseline and modified binaries; lower or equal p95 time where claimed, no heap/wdt/ghosting regressions, all persisted exits; verify C3 without PSRAM and S3 with PSRAM. Budget changes must be measured, not asserted. Sprint resource-budget task already exists.
**Risk:** critical if memory ownership and FreeRTOS concurrency are wrong.

### P1-E — One-time legacy stats and awards import, not a second tracker

**Sources:** CrossVi VCodexStatsImporter.cpp + VCodexStatsImporterTest.cpp; InkHome ReadingStatsData.h / Achievements.h and 5-minute idle/30s session heuristics; BookPoint StatsStore.cpp.
**Existing:** Kaneo #2, #3, #8, #18.
**Change:** standalone import into canonical YBX account/session model through explicit migration. Detect vCodex formatVersion 6 and optional InkHome data with strict bounds; build preview (books/days/sessions), identify stable book IDs and paths, dedup event IDs, mark completed migration transactionally. Never accumulate a second timer or infer cloud award unlocks from locally computed vCodex badges. Provide offline export/backup option and safe retry/rollback.
**Acceptance:** repeat import, renamed books, missing clocks, empty history, large JSON, invalid dates, reused titles, stale achievements, interrupted import, server/reader conflict; no double counted sessions or overwritten newer server events. At device level distinguish imported vs synchronized data.
**Risk:** very high for user data; require migration fixtures.

### P1-F — EPUB normalization and content-identity-preserving delivery

**Sources:** epub-tailor X3/X4 profiles, image/encode.rs, html/tables.rs; CWA Bridge feeds.py/cache.py/optimizer.py as independent protocol/test reference; Library Manager optimizer.rs as design-only comparison.
**Target component:** YBX Server/ingestion, NOT MCU hot-path.
**Change:** cache content-derived variants for X3 528×792, X4 480×800, and measured X4 Pro 800×480 screen; grayscale baseline JPEG / 8-bit PNG, bounded chapter sizes, image area/bytes, CSS, font removal, SVG rasterization, careful table/code/footnote handling. Preserve original source and immutable logical book identity; store transform version, content hash and derived file version. Serve via existing auth OPDS/reading shelf, not user-provided arbitrary proxy URL.
**Critical dependency:** KOReaderSync document hash and EPUB position IDs may change when re-encoding! Design canonical book ID + original/variant content hashes and explicit progress migration, or leave sync-critical books as original. Do not silently replace a file in place while a reader tracks its document hash.
**Acceptance:** EPUBCheck/round-trip fixtures (images, RTL/CJK, chapters, anchors, tables, SVG, footnotes, oversized images); same book across YBX Desktop/PocketBook/X3/X4; stable progress mapping or intentional "new version" UX; no ZIP bombs/path traversal/SSRF. Keep GPL-source copying separate from MIT implementation.

### P1-G — Scripted firmware-level QEMU regression gates

**Source:** serialx/eink-qemu already at 0.9.2; YBX emulator existing branch.
**Existing:** Kaneo #26–38, #14.
**Change:** binary-level C3 and S3 scenarios: boot+SD image, button/touch, native YBX Home, ebook open/page/search/footnote, memory snapshots, offline/expired auth, dynamic OPDS, sleep/update/reboot, EPD panel lifecycle. Pin emulator SHA/profile per run. Recognize unsupported peripherals and surface "not tested" rather than "pass".
**Acceptance:** repeatable one-command scripts, reference boot reports/screenshots and negative cases, emulator failures separately classified from firmware defects, no invented battery/runtime measurements. This **supplements, never replaces** physical HW tests.

### P2-H — Native Apps framework with explicit platform caps

**Sources:** InkHome AppRegistry.h/.cpp, AppsActivity.h, AppPaths.h; CrossLight UtilityRegistry.cpp as comparison.
**Target:** reusable YBX Native Apps menu connected to existing activity manager; no rewrite of native YBX home.
**Change:** compile-time registered AppDescriptor (unique ID, localized title, create factory, device/feature gate, byte budget). One app = portable pure C++ core + bounded Activity UI + isolated /apps/id read-only content and /.crosspoint/apps/id state. Allocate on enter/free on exit; no extra thread/network by default. Distinguish app from PluginHub web/SD integration, document reflash required for new binaries.
**Acceptance:** empty registry, unknown/malformed content, no SD, repeated open/close, navigation/back, X3 portrait, X4 rotated, X4 Pro touch, heap watermark and flash delta, disable via compile flags. First launch only after P0/P1 stability.

### P2-I — Markdown/notes/highlights and portable export

**Sources:** PaperBit MarkdownSection/MarkdownToBoxParser, NotesListActivity, NoteEditorActivity; InkHome NotesFormat/Highlights/SendNote; BookPoint FootnoteTextExtractor.
**Change:** reader for .md with stable pagination/resume/TOC, selection and highlights, optional Markdown draft editor; YBX-owned clip identity and eventual sync with Desktop/PocketBook. Use stable book IDs and collision-free filenames, not title alone; write .tmp+backup atomically; support Unicode. Web "send note" must have local authentication/CSRF controls when exposed.
**Acceptance:** long Markdown, bad UTF-8, huge lines, code blocks, headings, link/navigation, 1000 highlights, duplicate book titles, SD full, crash mid-write, offline conflict and cross-client import. Distinct from YBX Server's own notepad sync contract until defined.

### P2-J — Offline dictionary, footnotes and reader controls

**Sources:** YACP Dictionary.cpp (StarDict lookup), BookPoint FootnoteTextExtractor, IS7V4N keypad dictionary UI, Hairline visual concept, InkHome Motion.cpp.
**Existing:** upstream CrossPoint already has dictionary/TOC/bookmarks/footnotes and X3 tilt capability; do not fork new engines.
**Change:** focused improvements only: expandable footnotes and return navigation, dictionary lazy SD lookup and fallback, small unobtrusive bottom progress strip, optional motion shortcuts behind X3 hardware/capability checks. Device-specific wake/latch must remain conservative.
**Acceptance:** no reader layout damage/ghosting, correct focus and exit, malformed dictionary, no SD, no sensor and accidental shake, distinct X4 Pro touch handling.

### P2-K — Bilingual reading as isolated optional mode

**Source:** CrossLingua src/modules/lingua/{modes,layout,services}; Translate engine as capability reference.
**Change:** first ship side-by-side/interleaved/tooltip for **pre-translated** EPUBs, reusing parser and bounded layout; prefer server preprocessing when external translation is wanted. Separate translations from source text, preserve paragraph alignment, book identity and sync progress. No embedded third-party API keys, raw book upload or always-on HTTP.
**Acceptance:** English/Russian/CJK/RTL fixtures, large bilingual books, image-heavy chapters, column width on C3, cancellation/timeout, max contiguous heap, offline mode; fallback to original text. Gate C3 if not within memory budget.

### P2-L — Book browsing and covers without duplicate library model

**Source:** CrossLingua CoverGrid.cpp/CoverGridLayout.h; BookPoint home themes; CrossVi thumbnail caches.
**Existing:** YBX native Home/Library/Account already under development (#6, #7, #16, #19, #22).
**Change:** adopt incremental thumbnail loads/negative-cache placeholders, selection redraw on changed cell only, bounded book-page count and metadata fetch. All actions use real YBX provider/source permissions; no fabricated fetch actions or stale cache presented as live.
**Acceptance:** 1000+ books, corrupt/missing covers, portrait/landscape, touch and remote controls, no double download/duplication and no unnecessary Wi-Fi in Home.

### P2-M — Content acquisition: saved articles, news, catalogs, read-state

**Sources:** Instapaper converter Worker, Newspaper RSS EPUB, Readloop OPDS/Readwise proposals, CWA Bridge feed rewriting.
**Change:** YBX Server ingestion adapters for opt-in RSS digest, Instapaper and optionally Readwise; deliver normal authorized EPUB/OPDS to native reader. Keep provider credentials server-owned, sanitized article HTML/images, stable article IDs and deletion/archive policy with explicit user intent. Readloop "seed-progress" is a proposal, not accepted public API: implement only after KOSync compatibility and authorization tests.
**Acceptance:** duplicate input, update to article, malformed XML, pagination, 429 and backoff, no Wi-Fi reader, unauthorized endpoints, document hash, archive-on-finish idempotency, no accidental archiving of a similarly titled article.

### P2-N — Optional study utilities (only after native app framework)

**Sources:** InkHome Inkcards/Sm2.cpp and SleepCard.cpp; InkVerse, PocketWiki/PwaReader.cpp, FocusTimer, InkRecipes; PaperBit BLE keyboard; CrossLight utility registry.
**Change:** favor two small, selectable profiles: flashcards/SM-2 + offline reference, and Markdown notes/writer. Configure apps out of production builds by default until there is headroom; optional sleep flashcard is a provider of sleep decoration only, never blocking sleep event delivery. Reuse built-in resources only when licence reviewed.
**Acceptance:** pure host tests, flash storage deltas, bounded deck/file sizes, no BLE+Wi-Fi heap lock, sensorless board gating, no idle power regression. Other apps (games, cooking, Bible, Claude) are not essential to YBX Reader and should not inflate the base.

### P2-O — Web flasher, dual-firmware recovery and USB transport (investigation)

**Sources:** InkHome docs/flash/flasher.js, SlotGuard, FirmwareSwitchApp; PaperBit SerialFileTransfer.cpp; BookPoint USB Drive and recovery.
**Change:** separate experimental recovery-tool branch; detect and compare bootloader/otadata/partition geometry and image identity. Default update must protect the last known-good slot and maintain verified YBX recovery. A stock+YBX co-install may not be possible/safe across all X3/X4/X4Pro partitions, encrypted/USB-locked units and YBX image sizes. Inspect image SHA-256, actual written MD5, partition boundaries, boot selection and rollback. Avoid bypassing esp_ota_set_boot_partition checks. X4 Pro already has USB MSC; only add USB serial protocol if there is a documented gap.
**Acceptance:** simulated geometry matrices (stock, crosspoint, YBX, unknown, blank, corrupt otadata), flash failures/readbacks, no overwrite of sole bootable image, signed physical acceptance separately. **No install/flash on user hardware as part of research.**

### P2-P — Privacy-safe lost device / sleep widgets

**Sources:** Lost XTE Tag app.js, InkHome owner overlay, stats cards, wallpaper/clock, YACP sleep image tonal processing, Hairline appearance.
**Existing:** YBX sleep statistics card is under construction (#13).
**Change:** keep YBX canonical card first; allow configurable owner contact QR/strip only with informed opt-in; optional quotes, next flashcard, clock and book cover. Sleep image should use cached SD data and strict timeout/fallback, no fetch required to enter sleep. Inspect per-mode blackout/ghosting and image byte budget.
**Acceptance:** disabled by default for identity details, offline and SD-error fallbacks, zero live network required, no regressions in P0 sleep/event delivery.

### P3-Q — Hardware portability and diagnostic extras

**Sources:** StreamlinedStartup E1001, CrossMono BMP converter/test size gate, IS7V4N 4MB config, BookPoint X4 Pro RTC/power, InkHome battery gauge/motion, YACP autonomy.
**Change:** preserve FreeInk SDK as HAL owner. Add profiles only for explicitly supported hardware. Offer diagnostic battery capacity/cycle data if actual board gauge supports it; never claim inaccurate metrics on other boards. Consider 4MB build only as a separate, intentionally limited product.
**Acceptance:** separate board build/profile, correct RTC/gauge detection, no writes on unsupported devices, hardware evidence. Not required for X3/X4 baseline release.

## 3. Sequencing and dependency graph

**Stage 0 / freeze candidate:** P0-A -> P0-B; coordinate existing Rate Cloud gates.
**Stage 1 / reliable reader:** P1-C + P1-D + P1-G -> P1-E (stats import) -> P1-F (EPUB service), all with shared identity tests.
**Stage 2 / product parity:** P2-L (library UX) + P2-I (Markdown/notes) + P2-J (dictionary/footnotes). Add P2-H only for extra apps, then P2-N.
**Stage 3 / expansion:** P2-K (bilingual), P2-M (articles/news), P2-P (sleep widgets), P2-O (experimental flasher), P3-Q only where targeted hardware exists.

**Parallel work is safe only across boundaries**: server EPUB service and firmware recovery can proceed independently, but any change to OTA, settings, SD persistence or the reader session lifecycle must serialize through a single shared integration review.

## 4. Acceptance matrix to attach to each implementation PR

| Gate | Mandatory evidence |
|---|---|
| Scope | Linked Kaneo task and source commit/file, clear intended functional delta; duplicate/upstream check |
| Trust | licences/notices reviewed; correct TLS CA/hostname; redacted diagnostics; no secrets in SD/browser logs |
| Compat | existing YBX account, provider token, plugin version, schema; vCodex local history migration non-destructive |
| Identity | same logical book through optimized EPUB, local bookmark, KOReaderSync hash and YBX Server history |
| Durability | crash/short write/disk full/corrupt file/rollback; idempotent session retries and staged checkpoint |
| Performance | repeatable flash/static RAM/free heap/max allocation measurements per X3/X4/X4 Pro; baseline delta |
| Navigation | host unit tests, FreeInkUI simulator, C3 and S3 emulator with button/touch/rotation/sleep scenarios |
| Hardware | real-device evidence required for public release; absence logged explicitly, never silently PASS |
| Release | signed-off board matrix, stable version/asset naming, tested updater and recovery; no automatic prod merge |

## 5. Explicitly NOT adopting

1. Whole-fork merges from InkHome, CrossVi, CrossLingua, PaperBit, YACP or CrossLight (overwrites base and multiplies conflicts).
2. A second vCodex/InkHome local achievements account or another reading timer (YBX cloud is canonical).
3. CrossLight offensive Wi-Fi/BLE tools and unrelated cloud AI/Bible suites; uncontrolled internet calls from reading loop.
4. KOReader Hairline Lua code as a firmware plugin, or GPL/AGPL source copied into MIT-only firmware without licence analysis.
5. Disabling TLS/hostname validation or silently forwarding auth through redirects; unsafe OTA partition writes.
6. Bundling all InkHome built-ins (its C3 release is already ~94.5% of the app slot); native Apps are modular **at source level**, not downloadable compiled code.
7. A new full QEMU fork or older QEMU base when eink-qemu 0.9.2 is already adopted.
8. Device OTA release before validated physical wake/sleep/network/storage and rollback matrix.

## 6. Ready-to-start PR series and suggested tracking

Create fresh tickets only for scopes *not already present*; where applicable add these references to the existing Kaneo cards.

| Order | PR name / deliverable | Kanеo mapping | Depends |
|---|---|---|---|
| 00 | firmware candidate reconciliation and branch manifest | existing #15, #39 | — |
| 01 | bounded TLS/event/sleep regression gate | existing #3, #5, #11, #25 | 00 |
| 02 | durable settings/data writer + no-SD recovery | new ticket, sprint atomic-settings reference | 00, 01 |
| 03 | C3 memory/workspace and quick-resume benchmark | new ticket, sprint resource-budget reference | 00, 01 |
| 04 | emulator reading/recovery end-to-end suite | existing #26–38 | 00 |
| 05 | vCodex 6 one-time stats migration + dedup | existing #2, #8, #18 | 01, 02 |
| 06 | device-profile EPUB conversion + identity plan | new YBX Server task linked to #22, #21 | 00 |
| 07 | native reader catalog/cover performance | existing #6, #7, #16, #22 | 02, 03 |
| 08 | Markdown reader, bookmarks/notes export | new ticket | 02, 03 |
| 09 | plugin vs native app registry and minimal demo | new ticket | 03 |
| 10 | bilingual EPUB mode (pretranslated first) | new ticket | 06, 08 |
| 11 | RSS/Instapaper/Readwise ingestion OPDS | new YBX Server task | 06 |
| 12 | opt-in flashcards, offline reference / BLE writer | new ticket | 09 |
| 13 | safe stock/YBX alternate-slot flasher investigation | new experimental ticket, existing #14 | 00, 02 |
| 14 | optional owner QR / sleep themes | existing #13, optional new subtasks | 01 |
| 15 | hardware + OTA release gates | existing #14, #24 | all chosen release modules |

**Start now:** choose 00, 01, 02, 03, 04; product extras should not delay fixes for TLS/FIFO and broken SD handling. All work should be in reviewable branches with tests and actual provenance. No automerge and no production deployment are implied by this plan.
