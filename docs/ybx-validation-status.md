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

## Deadline-bounded transport (this branch)

The SecureNet deadline patch described in
[the SDK patch plan](ybx-sdk-patches.md) replaces the blocking
`lwip_getaddrinfo()` connect with an asynchronous lookup bounded by the same
deadline as the TCP connect, TLS handshake and TLS 1.2 retry.

Host checks: `sh freeink-sdk/libs/network/SecureNet/test/host/run.sh` passes
(scripted resolver: cache hit, NXDOMAIN, blackhole, abort, late answer after
timeout/abort, slot exhaustion and recycling, `millis()` wraparound).
Firmware build: `pio run -e default` passes.

Hardware acceptance (owner, on X3/X4 and X4 Pro) — record wall-clock duration
from the sleep trigger to radio-off, including WiFi stop and the SD write,
against each case:

- [ ] Wi-Fi up, DNS server unreachable: request fails within its timeout, the
      reader sleeps with the previous image, no watchdog reset.
- [ ] Wi-Fi up, NXDOMAIN: fast failure, no multi-second stall.
- [ ] TCP blackhole (port filtered): fails at the request timeout.
- [ ] Slow TLS handshake (throttled or 1.2-only host): bounded by the same
      timeout; HTTPS hostname/SNI still works (e.g. YBX catalog opens).
- [ ] Expired reader token: re-authorization still works after a failed
      request, and the retry shares the handler deadline.
- [ ] Cancel path (back/navigation during a connect): returns promptly and the
      radio stops; a late DNS answer does not crash.
- [ ] Repeat 10 delivery attempts: heap before/after shows no growth per retry.

Host tests cannot prove device timing; treat these as required before claiming
a sleep-latency bound.

## Outstanding

- Four independent final reviews are incomplete. OpenCode Go correctness,
  architecture and embedded attempts timed out without substantive results;
  Qoder documentation review failed on its credit limit. An earlier Home
  architecture review does not cover all final changes.
- DNS, TCP and the TLS handshake now share a request's timeout (SecureNet
  deadline patch), but the sleep drain budget remains cooperative: SD writes and
  WiFi shutdown are outside it, and an in-flight TCP/TLS wait still runs to
  that timeout. No hard upper bound on sleep latency is claimed.
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
