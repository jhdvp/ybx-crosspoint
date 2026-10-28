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
- The outbox is still bounded at 4 KB per plugin, now dropping the oldest
  whole events first instead of the whole queue; a device that stays offline
  long enough still loses its oldest events. Shared attempt limits and
  subscription order can starve later plugins.
- Local reading statistics ship as a native screen (see the section below).
  Account statistics, server streak/goal/achievements and cloud import remain
  unimplemented.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- Inherited HTTPS certificate verification remains disabled. Transport/OTA
  trust hardening is a separate release blocker.
- A `start_time` back-computed after a late clock sync is an estimate: the
  first epoch the session saw, minus the seconds already counted. It inherits
  any clock error and treats the counted dwells as contiguous.
- Only a screen that takes the activity (menu, dictionary, bookmarks, clipping,
  KOSync, frontlight panel) is excluded from reading time. Overlays the reader
  draws itself — the load-failure popup, the end-of-book suggestions — still
  count as reading.
- No device flashing, hardware validation, release or PR is part of these checks.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

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

## Native reading statistics (fw-native-stats branch)

This section covers the local statistics screen added on top of the session
journal. Account/cloud statistics are not part of it.

### Implemented

- The firmware advertises `stats.native.v1` and `ui.dashboard.v1` in the plugin
  capability string, so a catalog's statistics plugin is no longer filtered out
  for lacking them.
- A plugin manifest may declare `dashboard.source: "reading-stats.v1"`. The
  YBX manifest declares four pages: «Ваше чтение», «Неделя», «Календарь»,
  «Книги». The device renders `metric`, `week_bars`, `heatmap_28` and
  `book_list` natively; widget labels come from the manifest. Unknown widget
  types are skipped, unknown fields and `scope: current-book` render "—", and a
  page with no supported widgets is dropped. No layout, colors or animation
  from the manifest are interpreted beyond this fixed vocabulary.
- Data is the device's own finished `reader.session` intervals — the same
  sessions the YBX plugin receives, including the covered-screen exclusion —
  recorded even without a YBX subscription: `/.crosspoint/reading-stats.bin`
  (totals and a 64-day ring, 568 bytes) and `/.crosspoint/reading-books.bin`
  (at most 32 books, 3,336 bytes). Both are written through a temp file and
  `Storage.replaceFile`, with a leftover temp file recovered on load.
- Sessions are deduplicated by the reader-session event id. A session spanning
  local midnight is split across the two days while the session count stays on
  the first. An unset clock keeps the seconds in the totals as "undated" and
  the screen says the clock is not set; it never labels them as today.
- A day the device recorded nothing for is drawn as "no data", not as zero.
  The footnote shows when the newest session was recorded, or up to which day
  the data is current when the clock was unset.
- The screen is opened from the plugin list (directly on non-catalog devices,
  via «Статистика»/«Каталог» on catalog devices). Back, left/right and touch on
  the tab row switch pages; the book list pages through «Ещё» with Confirm.
- The bundled YBX `plugin.js` now says that its profile is server statistics
  and that the reader-local counters live in the statistics screen, and that
  the vCodex export is not imported by this firmware.

### Verified on the host

- `~/.platformio/penv/bin/pio run -e default` succeeds; the figures are in the
  build log under `.build-logs/`.
- The `test/reading_stats` gtest suite runs in the CMake/CTest host-test build:
  it covers numbering, midnight and DST splitting, ring eviction, dedupe by
  session id, streaks, UTF-8 truncation, CPS1/CPB1 round-trips, corruption and
  buffer-too-small handling.
- Device rendering, touch, rotation and real storage timing are not covered by
  these tests; see the checklist below.

### Limits and honest gaps

- The four pages are local data only. Server streak, year goal and achievements
  need a later capability/data source; the manifest does not declare them.
- A session ended by a crash or power cut never reaches local statistics (the
  cloud upload still recovers it from the journal checkpoint). Only sessions
  finished normally (exit, sleep, restart) are recorded locally.
- The ring keeps 64 days and 32 books; older days still count in the totals and
  the active-day count but are not shown in the calendar or book list.
- The calendar is the fixed 28-day `heatmap_28` widget. Month navigation,
  per-day detail and page-turn metrics (`qualified_turns`,
  `seconds_per_turn`) are not implemented and render as "—" or are absent.
- The vCodex export button remains for firmwares with an Import Statistics
  page. The native statistics store never reads or writes
  `/.crosspoint/reading_stats.json`, and nothing on this firmware imports server
  history, so a cloud snapshot cannot overwrite local data or double-count
  minutes. The export button still writes that file when the user asks for it.

### Owner acceptance checklist: native statistics

No hardware result is claimed here; these steps are the owner's.

1. Empty state: with no recorded sessions, open the statistics screen. Expect
   «Пока нет данных о чтении» and no invented numbers or calendars.
2. Record a session: read a book for at least a minute across a few pages,
   leave the reader, reopen the screen. Expect today and total time to include
   that session, the book to appear in «Книги», and the footnote to show its
   time.
3. Midnight split: set the clock shortly before local midnight, read across it,
   then check that the session is split between two calendar cells while the
   session count increased by one.
4. Unset clock: boot without a trusted time source, read, then open the screen.
   Expect today to show "—", the footnote to say the clock is not set, and
   total/undated time to still include the session.
5. Single count: read for a minute, then send the device to sleep (the flush
   runs on the pre-sleep path and again on teardown). Sessions and minutes must
   grow by one session only. Reboot afterwards: the cloud retry of the same
   session (same id) must not change the local numbers.
6. Covered screen: open the reader menu and keep it open for ten minutes mid
   session; the statistics must not gain those minutes.
7. Sync upload: with the YBX plugin connected, let a session upload, then open
   the screen. Local minutes must not change because of the upload, and a
   normal sync must not create or modify `/.crosspoint/reading_stats.json`
   (only the explicit export button may write it).
8. Tabs and books: switch all four pages with the side buttons and by tapping
   the tabs; with more than five books read, page the book list with Confirm on
   «Ещё».
9. Rotation and both devices: repeat 1-2 in portrait/landscape on X3 and X4;
   the layout must stay inside the safe area with button hints visible.
