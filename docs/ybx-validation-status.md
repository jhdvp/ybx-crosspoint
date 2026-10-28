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
- Native account statistics and achievements are planned, not implemented.
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
