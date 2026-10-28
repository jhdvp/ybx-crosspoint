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
- `ReaderSession` reports every reading interval: successful ordinary page
  turns in either direction plus the trailing interval when reading stops at
  exit, sleep or restart. Intervals are bounded to 2-1800 seconds; a session
  shorter than 5 seconds is not reported. Time while another screen covers the
  page (reader menu, dictionary, bookmarks, clipping list, KOSync, frontlight
  panel) is excluded: the dwell closes when the page loses focus and the render
  after the screen returns starts the next one. A clock that arrives
  mid-session back-computes `start_time` instead of leaving it `0`.
- A session id is allocated before its first checkpoint or flush and
  checkpointed with the summary at `/.crosspoint/reader-session.journal` at
  most once a minute, in EPUB, XTC and TXT readers alike.
  A checkpoint left by a crash or power loss is queued at the next boot with
  the same id, so the server's `(device, event_id)` key makes the retry
  idempotent. Exit, sleep and restart flush the final summary before the
  activity or chip goes away. A checkpoint that belongs to another session is
  never overwritten or cleared: an unqueued session survives the activity
  teardown that follows the pre-sleep flush, and survives a failed recovery
  attempt instead of being replaced.
- A journal write that fails (full or write-protected card) is retried on the
  next minute rather than on every loop pass, and a read prefers the staged
  `.pending` copy whenever both parse, so a newer short checkpoint cannot be
  displaced by an older longer one.
- At the 4 KB outbox cap the oldest whole events are dropped first instead of
  the whole queue, and event lines may be 1024 bytes, so a session whose book
  path is long is no longer dropped.

## Verification

- Host suite: **930/930 passed**, including the reader-session delivery cases
  (exit, sleep, restart with an open book, offline backlog, duplicate delivery)
  and the queue, storage and deadline cases. The review follow-ups added nine
  cases: covered-screen and late-clock sessions, the staged-checkpoint
  preference, and the sleep-path double flush, failed recovery and
  write-failure retry (the last three drive the modelled device lifecycle in
  `test/reader_session_delivery`, not `ReaderActivity.cpp` itself). Storage
  tests cover short writes, failed renames/rollback, interrupted transaction
  boundaries and all 511 partial-append positions.
- Formatting (`bin/clang-format-fix`, clang-format 21) and `git diff --check`:
  passed.
- `default` build: passed; static RAM 66,712/327,680 bytes, flash
  5,550,225/6,553,600 bytes.
- The release-configuration `gh_release_rc` and `x4pro-gh_release_rc` builds
  were verified on the parent state but not re-run after this change.
- FreeInk SDK remains pinned at
  `44427f8d51866848b26bea01197f499f9fee5c4b`, without local SDK changes.

Static build sizes are not runtime heap measurements. Host storage models do
not establish physical FAT/SD durability. Build logs, source patches and binary
checksums were saved locally in `artifacts/ybx-sleep-recovery-20261028` in the
parent YBX workspace; generated binaries and logs are not committed here and
this worktree keeps no build logs.

## Outstanding

- Four independent final reviews are incomplete. OpenCode Go correctness,
  architecture and embedded attempts timed out without substantive results;
  Qoder documentation review failed on its credit limit. An earlier Home
  architecture review does not cover all final changes.
- Synchronous SDK DNS/TCP and SD operations can exceed the cooperative sleep
  budget. No hard upper bound on sleep latency is claimed.
- The inherited 4 KB outbox overflow policy still drops the queue wholesale;
  shared attempt limits and subscription order can starve later plugins.
- The first native account-summary/cache and Statistics screen slice is now
  implemented separately; see [Native statistics](ybx-native-statistics.md).
  Achievements, local journal/calendar and per-book history remain pending.
- X4 Pro now uses the YBX Home with shared touch routing. Hardware acceptance,
- The outbox is still bounded at 4 KB per plugin, now dropping the oldest
  whole events first instead of the whole queue; a device that stays offline
  long enough still loses its oldest events. Shared attempt limits and
  subscription order can starve later plugins.
- Account statistics and achievements now have YBX-backed native summary panels; local reading statistics are collected on-device. All hardware checks remain open.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- HTTPS paths carrying tokens or downloads now use peer/hostname validation,
  bounded SNTP for unset clocks and downgrade/origin checks. The local TLS patch
  series passes host tests, the C3 compiler and a live trust-anchor/hostname
  check for six service endpoints. No device handshake or field test is claimed.
- User-supplied KOReader/KOSync servers still have documented setInsecure()
  exceptions; these are not covered by the first-party trust boundary.
- A `start_time` back-computed after a late clock sync is an estimate: the
  first epoch the session saw, minus the seconds already counted. It inherits
  any clock error and treats the counted dwells as contiguous.
- Only a screen that takes the activity (menu, dictionary, bookmarks, clipping,
  KOSync, frontlight panel) is excluded from reading time. Overlays the reader
  draws itself — the load-failure popup, the end-of-book suggestions — still
  count as reading.
- No device flashing, hardware validation, release or PR is part of these checks.

## Native KOSync setup after pairing — 28 October 2026

A separate task from the sleep-recovery work above; the same limits apply.

### Implemented

- `src/util/YbxKosyncDecision.h` — the pure policy: apply, adopt, keep a
  hand-typed server, give the server back, clear, or forget the derived
  configuration; the cross-client consent threshold; the status precedence; the
  age buckets. Host-testable without storage, HTTP or UI.
- `src/util/YbxKosync.{h,cpp}` — reads the reader key from the YBX plugin folder
  (existing bounded 2 KB token read, legacy path included) and writes the
  existing `KOReaderCredentialStore`: username `ybx`, the key as the password,
  the `https://my.ybx.app/api/v1/crosspoint/kosync` bridge, content-hash
  matching, smart sync. It owns no second protocol, timer, queue or connection,
  stores no provider OAuth and never logs the key.
- Reconciled at three moments only: opening CrossPoint Sync settings, starting a
  reader sync, and saving a plugin token after pairing. Boot loads the switch
  and history without touching plugin files.
- Settings list prefix: the YBX switch, its status with the age of the last
  transaction, and a "Use YBX sync" row that appears only when a hand-typed
  server conflicts with a paired key. Opting out clears only what this module
  wrote.
- `KOReaderSyncActivity` records sent / applied / in sync / offline / re-pair /
  refused / failed, and a remote position more than one percentage point ahead
  asks before the reader leaves its place, like the reference clients.
- 17 status strings through the existing generator: English and Russian written,
  other languages English copies.

### Verification

- Host suite: 909/909 passed, including 11 new `YbxKosyncDecision` cases.
- Firmware `default` build: passed; static RAM 66,760/327,680 bytes,
  flash 5,551,763/6,553,600 bytes (84.7%). No unmodified-`default` baseline was
  measured in this session, so the growth caused by the change is not stated.
- `./bin/clang-format-fix` and `git diff --check`: passed.

No device flashing, heap, latency or radio measurements were taken, and none are
claimed for this change.

### Owner acceptance checklist

YBX plugin installed, X3 or X4:

1. Pair by QR, then open Settings → CrossPoint Sync. Expect two leading rows:
   the YBX switch on and a status line, no "Use YBX sync" row. The username,
   server and password rows must already show the YBX values with nothing typed.
2. Read to about 30%, close the book, reopen the sync settings. Expect
   "Sent, N min ago".
3. Open the same book in another YBX client (Desktop or Classic) and read
   further. On the reader, open the book and sync. Within one percentage point
   the reader must jump silently; further ahead it must ask first. Choose to
   keep reading here, then Back: the reader stays on its own page.
4. Repeat step 3 for a Yandex, a WB and a LitRes download and for a locally
   normalized EPUB; each must resume at the same place across clients.
5. With no network, sync: expect "Offline". Restore the network and sync again:
   the local position must be unchanged by the failed attempt.
6. With YBX on, type another server URL and password by hand and leave the
   screen. Expect "Manual server" plus the "Use YBX sync" row; that row must
   move the sync back to YBX. After the hand edit, reopening the screen must not
   overwrite it again.
7. Switch YBX sync off. Expect the credentials and the YBX URL cleared and
   "Off"; a hand-configured server must survive the switch. Switch back on with
   the key on the card: the sync is configured again.
8. Turn the plugin system off or remove the plugin folder. Expect the YBX rows
   gone and any manual sync settings untouched.
9. Re-pair after the reader key expires or rotates. Expect the sync re-pointed
   without opening a keyboard, and no key text in the serial log.
## YBX aggregated library tree — 28 October 2026

The merged library (`/library`: Все, Читаю, Хочу прочитать, Прочитано, Серии,
Полки, Загруженные) and the per-source folders are OPDS 1.x Atom behind HTTP
Basic device auth. Those routes reject a `cp_` plugin token, and the JSON
plugin API has no tree, so the firmware browses the tree with the existing
native OPDS browser (`OpdsBookBrowserActivity` and the pinned SDK
`OpdsClient`/`OpdsFeedParser`) instead of a parallel JSON UI. Search,
OpenSearch, paging and signed `?e=&s=` downloads stay on that code path; no
bearer token is put into a URL, and the download log prints the URL without its
query so a capability signature never reaches the serial dump.

- One feed per level: root → «Все»/source → section → shelf/series → books.
  The server pages at `opds_page_size` and the SDK keeps at most 62 entries per
  feed; the change adds no heap-resident list.
- Back returns to the feed level it came from with the row the reader was on
  selected (arrow scrolling counts, not only an activated row). Leaving a book
  card and the reload after a download also return to that card.
- An entry with no acquisition URL (an OPDS 2.0 self-only row) opens a card
  marked "Not available": the primary button is not drawn and neither Confirm
  nor a tap there starts anything. In a flat plugin catalog the same applies to a
  row whose mapped `url` field is empty (the YBX update notice) once the
  manifest declares `download.require_url`; every other catalog keeps its fetch
  button. See [plugin manifest schema](sd-plugins.md).
- `STR_OPDS_UNAVAILABLE` gained a Russian text; other languages still fall back
  to English as the generator documents.

Verification: `test/opds_library_tree` compiles the pinned SDK Atom parser for
the host and pins the server-shaped feeds (root navigation with «Все» before
source folders, the Все folders, a shelf with a notice entry and signed book
cards, a series list with counts and paging, ordered series books, search
groups and facets, the 62-entry budget). `test_item_downloadability.py` compiles
`PluginCatalogActivity::itemDownloadable()` and pins which manifest shapes lose
the download action. Host suite: **907/907 passed**, including the eight parser
cases and the downloadability checks. `pio run -e default`: passed; static RAM
66,712/327,680 bytes, flash 5,545,589/6,553,600 bytes. Neither proves device
behavior.

Not verified: hardware. YBX feeds omit `opensearch:itemsPerPage` and
`startIndex`, so the "page N/M" indicator stays unknown while arrow paging
works. The pinned parser drops an entry whose only link is a `text/html`
alternate; YBX does not emit that shape in these feeds. A book the Yandex
source cannot deliver is still downloaded as an explanatory error EPUB (server
contract, not detectable by the client before the transfer). The notice-row
marking also depends on the manifest: the SD copy in this repo declares
`download.require_url`, and the plugin build served by the YBX site needs the
same field before an installed-from-hub catalog marks the notice.

Owner acceptance on the device (Wi-Fi configured):

1. Settings → OPDS Servers → YBX server
   (`https://my.ybx.app/opds/<device-id>`, Basic username = device id, password
   = device key; with no plugins installed the same browser is Home →
   Catalogs) → Browse.
2. The root lists «Все» first, then the connected source folders. Open «Все»:
   Читаю, Хочу прочитать, Прочитано, Серии, Полки (Yandex linked), Загруженные.
3. Серии → a series: books in series order; the number reads in the card.
4. Open a book card → Back: the same row is selected. Download → after the
   reload the same row is selected and the EPUB appears in the library.
5. Scroll with the arrows to a row that is not the first and open nothing.
   Search from the header: results keep the quoted-query header; Back returns to
   the previous feed with that scrolled row selected, not the top of the list.
   A Next/Previous page turn returns to the same row of the page it left.
6. Next/Previous move through a section; an unknown N/M count is expected.
7. The YBX plugin's update-notice row shows "Not available" and its card has no
   fetch button; Confirm on that card starts no transfer and Back returns to the
   list. Other plugin catalogs (and any row with a file URL) keep their button.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

## Native on-device statistics merge (29 October 2026)

Native reading statistics from `agents/fw-native-stats` are displayed separately from the remote account summary. The local screen reads completed reading sessions and is meaningful offline. Source-level and host acceptance require tests; this entry is not hardware or production acceptance.

## Subsequent native summary slice

The checks above describe the earlier saved sleep/Home commit. New native-summary
work has its own final builds, review packet and test results in the parent
workspace under `artifacts/ybx-native-statistics-20261028`. No sleep/TLS blocker
is closed by adding these screens. The SD plugin contract is now1.8.0.
## Owner acceptance checklist: reading-session delivery

No hardware result is claimed here; these steps are the owner's.

1. Exit: read a book for at least 10 seconds across two pages, then leave the
   reader. Expect one `reader.session` in the plugin outbox, delivered at the
   next online drain with that reading time and final progress.
2. Sleep: read, then let auto-sleep fire or press sleep. With the YBX plugin
   connected and battery above 20%, the device should join WiFi, deliver
   `reader.session` (and `sleep.enter`), then sleep.
3. Restart with an open book: trigger the loan/KOSync time-sync restart path
   while reading. After the reboot the session should still be delivered once.
4. Crash recovery: read an EPUB for at least two minutes, then cut power (hold
   the power button or pull the battery). On the next boot the checkpointed
   session should be queued and delivered once. Repeat with an XTC or TXT book:
   those readers share `ReaderActivity::loop()` and its checkpoint.
5. Offline backlog: read several short sessions with WiFi unavailable, then
   let the device go online. All queued sessions should arrive; if the outbox
   exceeded 4 KB, the newest events should have survived.
6. Duplicate check: after step 4, reboot once more. YBX reading time for that
   session must not double (the recovered event keeps its original id).
7. Covered screen: read for about a minute, open the reader menu or look a word
   up in the dictionary and leave it open for ten minutes, return, turn one
   page, then leave the reader. `duration_seconds` should be roughly the two
   reading spans, not the ten covered minutes.
8. Sleep with no network: read, then sleep while WiFi is unavailable. The
   session should still reach the server after the next online boot: the
   pre-sleep flush and the activity teardown that follows must not clear each
   other's checkpoint.
9. Failing card: with the SD card write-protected (or full), read for a few
   minutes. The serial log should show at most one `RSES: session checkpoint
   failed` per minute, not one per loop pass.
10. Late clock: read on a cold device whose clock is not set, let the time sync
    finish mid-session, then leave the reader. The delivered session should
    carry a plausible `start_time`/`end_time` instead of `0`/`0`.
## On-reader shelf parity — 28 October 2026 (this branch, review fixes applied)

Code presence only; no device run was performed. The change adds, in
PluginCatalogActivity, HomeActivity, CatalogActivity, FileBrowserActivity,
PluginHttp and the new `src/util/PluginShelf.h` policy header:

- "На ридере" row/detail state is shown only when ALL shelf gates hold:
  the manifest declares `download.dest_dir` (a config `dest_dir` override can
  move real downloads but never what the shelf plans, opens or deletes), the
  planned name is one safe filename component with a reflowable (`.epub`,
  `.txt`, `.md`) or XTC (`.xtc`, `.xtch`) extension, and BOTH the file and a
  companion marker `<file>.downloaded` exist. The marker is written only by
  the catalog download flow after the file passed verification, so a user's
  own same-named file in `/books/ybx` (or any other folder) is never displayed
  as on-shelf and can never be deleted through this feature. A marker whose
  book was deleted outside the shelf (e.g. in the file browser) is cleared on
  the next shelf scan, so it cannot later vouch for a copied-in file.
- Tap/Confirm on such a row opens the book (silent reboot into the reader, the
  established LCP-loan pattern) instead of downloading again; the open path
  re-checks the gates and refuses files the reader cannot parse.
- DONE screen after a download: Back is checked before any tap (a header back
  tap is also a screen tap and must not arm the reader reboot); the "Открыть"
  label and the tap-to-read hint appear only when the downloaded file's
  extension is reflowable/XTC — for any other extension the screen shows plain
  completion and Confirm returns to the catalog instead of rebooting into a
  failed reader load. `book.downloaded` now fires from `downloadFinished` only
  after the ZIP-magic check passed, so a rejected body is never announced to
  the server outbox.
- Long-press on an on-shelf row: "Скачать заново / Убрать с полки / Отмена".
  Removal is behind a confirmation dialog and deletes only the shelf-gated
  file (manifest `dest_dir` + book extension + firmware marker), its
  rebuildable caches, its recent-books entry, its `.downloaded` marker and the
  plugin sidecar (`{dest}.meta.json`) resolved through the same template the
  writer used. Settings, tokens, credential stores and every other folder stay
  untouched; when the destination folder cannot be created the download fails
  (no SD-root fallback), so planned and written paths cannot diverge.
- YBX home More popup gains a first "На ридере" entry: the download folder is
  resolved lazily on the first More press and cached for the home instance (no
  SD reads or JSON parsing on the home enter path). It opens the FileBrowser
  locked to that folder: short Back, long-press Back and a missing folder all
  stop at the shelf root instead of climbing to `/books` or the SD root.
- Finished `.epub` downloads are checked for ZIP magic (rejected as "Сервер
  прислал не файл книги", file removed). Storage write failures report
  "нет места/ошибка записи"; an empty server-provided download URL and a
  rejected filename now report distinct messages in every catalog.

### Host verification

- `pio run -e default` (X4 profile): passes after the review fixes; RAM
  66,712/327,680 bytes, Flash 5,550,937/6,553,600 bytes.
- Host suite (CMake/CTest under `test/`): 905/905 pass, including the new
  `plugin_shelf` suite (7 cases) driving `src/util/PluginShelf.h` — the exact
  dest_dir/extension/marker rules the shelf and removal paths call. No suite
  drives PluginCatalogActivity or HomeActivity UI; the other checks only prove
  the shared primitives (protected paths, safe removal, filename planning,
  i18n keys) still hold.
- Everything below is unverified until run on hardware.

### Owner acceptance checklist (device)

1. Catalog (YBX) → download a book → progress → "Книга скачана" → OK → reader
   opens that book at page 1; Back from the reader behaves normally.
2. Re-enter the catalog: the row reads "На ридере"; Confirm opens the book
   without network transfer; the footer button label is "Открыть".
3. Long-press the row → "Скачать заново" re-downloads and overwrites without
   extra prompts; → "Убрать с полки" shows the confirm with the book title;
   confirm removes the book, its `*.epub.downloaded` marker, its
   `*.epub.meta.json` sidecar and its caches/recent entry only (other books,
   settings, login intact); the row returns to normal download state.
4. Home → More → "На ридере" (offline, airplane Wi-Fi off): folder of
   downloaded books opens; open/rename/delete work; short Back at the folder
   and long-press Back never climb above `/books/ybx` (Back at the shelf root
   returns home). With the plugin absent or Plugin System disabled, the entry
   is not shown.
5. Copy a personal file into `/books/ybx` whose name matches a catalog row's
   planned name: the row must NOT read "На ридере" (no firmware marker) and
   long-press offers no removal; downloading the real book still writes the
   file plus marker. Then delete a firmware-downloaded book inside Home →
   More → "На ридере" and copy a personal file under the same name: the row
   must again NOT read "На ридере" (the orphan marker was cleared on scan).
6. On the DONE screen tap the header back arrow: return to the catalog
   without rebooting into the reader.
7. Server fault returning non-EPUB body → "Сервер прислал не файл книги", no
   file or marker left in `/books/ybx`, and no `book.downloaded` event in the
   server outbox; a valid download still announces exactly once.
8. Same book from two providers → two files (filenames carry ids) → both rows
   show "На ридере".
9. SD full or unplug mid-download → "Ошибка записи"/failure, no `.part`
   leftovers on the next attempt, retry works; deleting `/books/ybx` and
   filling the root so `mkdir` fails → "Ошибка записи" and nothing written to
   the SD root.
10. WB image-only book opens with cover; all four rotations (portrait,
   inverted, landscape CW/CCW) render rows, popup, DONE screen and shelf
   entries.
11. Repeat 1–4 twice to watch heap-driven behavior (auto-reboot is silent; no
   crash/reboot loops while browsing).

## TLS review integration (29 October 2026)

TLS hardening merged as a patch series into the native YBX/OTA code. Cross-origin credential stripping and OTA TLS errors remain separate from generic network and storage faults. Device acceptance and the ESP32-S3 TLS build remain distinct gates.
