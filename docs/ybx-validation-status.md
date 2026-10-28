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
- HTTPS requests that carry the reader token or download payloads verify the
  certificate chain and host name against a built-in CA bundle, refuse to start
  before a trustworthy clock (one bounded SNTP attempt), and never follow an
  https→http redirect or forward credentials to another origin. Delivered as a
  hash-guarded patch of the pinned SDK; see [the fork notes](../YBX_FORK.md).

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

## TLS verification checks

- Host suite: `TlsPolicyTest` 19/19 passed. It covers the clock-not-set gate
  (including a sync that claims success without a valid time), origin
  normalization (case, user info, default ports, IPv6), https→http redirect
  refusal, and the same-origin decision that controls credential forwarding.
  The full CTest run passed 917/917 cases.
- Size: the linked `firmware.elf` grew from 5,562,939 to 5,573,587 bytes of
  text (+10,648 B, about 10.4 KiB), 4,873 B of which is the PEM bundle; data
  and bss are unchanged. Runtime heap for a verified connection (wolfSSL CA
  parse and handshake buffers) is not measured here.
- Device acceptance still required:
  - YBX sign-in and catalog browse with a synced clock;
  - cold boot with Wi-Fi off: catalog/OTA must show the clock message and not
    connect;
  - OTA check and install across the GitHub → release-assets redirect;
  - a captive-portal or otherwise wrong certificate must show the certificate
    error and abort the transfer;
  - a user OPDS catalog outside the bundle must fail clearly rather than
    downgrade;
  - free-heap and largest-block measurements during a verified multi-MB
    download, compared with the previous build.
- Not verified on hardware: SNTP behavior after power loss, wolfSSL handshake
  memory/time on X3/X4, and root rotation as the bundle ages.

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
- KOReader sync and clippings sync still use `setInsecure()` with
  user-supplied servers, as does the unwired SDK OPDS implicit-auth flow; those
  paths are outside the token/download scope and are documented as exceptions.
- No device flashing, hardware validation, release or PR is part of these checks.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
