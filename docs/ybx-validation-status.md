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

## Upstream sync acceptance (maintainer, on device)

Host build and suites do not prove any of this. After each upstream merge, before
publishing a release, walk this list on an X3 or X4 with the current card backed up:

1. Update channel: About shows `YBX <version>`; "Check for update" reports the fork
   channel and never offers a `crosspoint-*` asset or another board's image.
2. Branding: boot and default sleep show the YBX mark in all four orientations; the
   device is reachable at `ybx.local`; the web header, favicon and footer links are YBX.
3. Home: empty device starts at Library; then one recent book, three recent books, a
   deleted recent file, a missing or corrupt cover, unknown progress, and a long
   translated title. Back resumes the latest book; no radio turns on while on Home.
4. Catalogs: `python3 scripts/install_ybx_plugin.py /path/to/card` keeps an existing
   `token.json` and `config.json`; zero sources, Yandex only, WB only, LitRes only and
   combinations; after unlinking one source, reopen YBX and confirm the folder is gone;
   an expired reader token offers device-code sign-in while a broken provider session
   stays a separate error.
5. Sleep events: the plugin sleep image still appears after a normal sleep, including
   when an earlier event failed, and the queue recovers after an interrupted write.
6. Reader regression that upstream now owns: RTL focus reading, reader menu selection
   and toolbar padding on one EPUB and one TXT/Markdown file, with progress cache reuse.
7. Resources: internal heap above 50 KB free with the largest block recorded across
   repeated Home, book, dictionary and Wi-Fi transitions.
8. Record the result: append the sync log entry with the integrated upstream SHA and
   leave any unresolved conflict or skipped check visible there.

See [the fork notes](../YBX_FORK.md), [the fork inventory](ybx-fork-inventory.md),
[the sync log](ybx-sync-log.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
