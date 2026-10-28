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
  shorter than 5 seconds is not reported.
- A session id is allocated before its first checkpoint or flush and
  checkpointed with the summary at `/.crosspoint/reader-session.journal` at
  most once a minute.
  A checkpoint left by a crash or power loss is queued at the next boot with
  the same id, so the server's `(device, event_id)` key makes the retry
  idempotent. Exit, sleep and restart flush the final summary before the
  activity or chip goes away.
- At the 4 KB outbox cap the oldest whole events are dropped first instead of
  the whole queue, and event lines may be 1024 bytes, so a session whose book
  path is long is no longer dropped.

## Verification

- Host suite: **921/921 passed**, including the reader-session delivery cases
  (exit, sleep, restart with an open book, offline backlog, duplicate delivery)
  and the queue, storage and deadline cases. Storage tests cover short writes,
  failed renames/rollback, interrupted transaction boundaries and all 511
  partial-append positions.
- Formatting (`bin/clang-format-fix`, clang-format 21) and `git diff --check`:
  passed.
- `default` build: passed; static RAM 66,712/327,680 bytes, flash
  5,549,521/6,553,600 bytes.
- The release-configuration `gh_release_rc` and `x4pro-gh_release_rc` builds
  were verified on the parent state but not re-run after this change.
- FreeInk SDK remains pinned at
  `44427f8d51866848b26bea01197f499f9fee5c4b`, without local SDK changes.

Static build sizes are not runtime heap measurements. Host storage models do
not establish physical FAT/SD durability. Build logs, source patches and binary
checksums were saved locally in `artifacts/ybx-sleep-recovery-20261028` in the
parent YBX workspace; generated binaries and logs are not committed here. The
session-delivery runs are logged in `.build-logs/` in this worktree.

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
4. Crash recovery: read for at least two minutes, then cut power (hold the
   power button or pull the battery). On the next boot the checkpointed
   session should be queued and delivered once.
5. Offline backlog: read several short sessions with WiFi unavailable, then
   let the device go online. All queued sessions should arrive; if the outbox
   exceeded 4 KB, the newest events should have survived.
6. Duplicate check: after step 4, reboot once more. YBX reading time for that
   session must not double (the recovered event keeps its original id).
