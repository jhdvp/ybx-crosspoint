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

## Verification

- Host suite: **897/897 passed**, including 22 queue, storage and deadline cases.
  Storage tests cover short writes, failed renames/rollback, interrupted
  transaction boundaries and all 511 partial-append positions.
- Formatting and `git diff --check`: passed.
- Final `gh_release_rc` build: passed; static RAM 66,696/327,680 bytes,
  flash 5,493,963/6,553,600 bytes.
- Final `x4pro-gh_release_rc` build: passed; static RAM 111,756/327,680 bytes,
  flash 5,350,302/6,553,600 bytes.
- FreeInk SDK remains pinned at
  `44427f8d51866848b26bea01197f499f9fee5c4b`, without local SDK changes.

Static build sizes are not runtime heap measurements. Host storage models do
not establish physical FAT/SD durability. Build logs, source patches and binary
checksums were saved locally in `artifacts/ybx-sleep-recovery-20261028` in the
parent YBX workspace; generated binaries and logs are not committed here.

## Outstanding

- Four independent final reviews are incomplete. OpenCode Go correctness,
  architecture and embedded attempts timed out without substantive results;
  Qoder documentation review failed on its credit limit. An earlier Home
  architecture review does not cover all final changes.
- Synchronous SDK DNS/TCP and SD operations can exceed the cooperative sleep
  budget. No hard upper bound on sleep latency is claimed.
- The inherited 4 KB outbox overflow policy still drops the queue wholesale;
  shared attempt limits and subscription order can starve later plugins.
- Native account statistics and achievements are planned, not implemented.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- Inherited HTTPS certificate verification remains disabled. Transport/OTA
  trust hardening is a separate release blocker.
- No device flashing, hardware validation, release or PR is part of these checks.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

## Settings / Updates / About parity lane (same date, separate change)

The numbers above belong to the sleep-recovery lane. For the Settings / Updates /
About reconciliation ([settings parity](ybx-settings-parity.md)):

- `~/.platformio/penv/bin/pio run -e default` — **passed**; static RAM
  66,712/327,680 bytes, flash 5,545,169/6,553,600 bytes, i18n blob 408,093 bytes.
  Re-run after formatting gave identical figures. Full log `/tmp/fsp-build4.log`
  (not committed).
- `./bin/clang-format-fix -g` — **passed**, and idempotent on the second run. The binary
  is not on `PATH` here and the script rejects anything below 21, so the gate ran with the
  clang-format **21.1.0** bundled in the installed Python `clang_format` package linked onto
  `PATH`; no repository file was changed to make that work. It re-packed the new `menuNames`
  table, which is the only formatting change it made.
- Host suite (CTest under `test/`) — **898/898 passed**, configured offline
  (`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<v1.17.0 copy>`, `-DANIMATEDGIF_SOURCE_DIR=<copy>`)
  because both `FetchContent` clones stall in this environment. Log `/tmp/fsp-host2.log`.
  The changed code is activity rendering and input handling, which the host harness does not
  cover, so this is a no-regression signal only.
- Four independent read-only handoff reviews (correctness, architecture, embedded,
  i18n/docs) ran against the candidate, plus a second targeted round over those fixes.
  Confirmed findings are all resolved in the same change: About position-sync gating, the
  unlabeled touch tap that could start a re-flash, the stale progress counters on a retried
  install, two doc citations, and one check that had been recorded as passing when it had
  not been run. Left open deliberately and written up in the parity document: the native
  support-log upload (blocked on the server auth contract), the whole-partition erase per
  retried install, and a visible on-screen Retry control for touch devices
  (`WifiSelectionActivity.cpp:926-941` is the precedent). Nothing in the second round was
  re-reviewed after the final formatting and progress-counter edits; those are mechanical,
  main-agent verified, and covered by the build and suite above. No device flashing or
  hardware validation is part of these checks.
