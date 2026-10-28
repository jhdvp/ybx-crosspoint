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

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
