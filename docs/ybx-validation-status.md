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

## OPDS catalog fetch bounds — acceptance

Browsing a catalog no longer blocks the UI indefinitely. Each logical catalog
operation (feed, search template, publication document, detail cover, LCP
license and unlock exchange) runs in an absolute 20 s window
(`src/network/CatalogFetchLimiter.h`) and each socket operation is capped at
the smaller of the window's remainder and 10 s. Back or Home during a fetch
cancels it and returns to Home. A timeout shows the usual error screen with
Retry (Confirm/tap) and Back; the message is "Server did not respond in time"
(`STR_CATALOG_TIMEOUT`) when the window expires, or the existing unreachable /
server-error message when a socket fails first. Book downloads are intentionally
not capped at 20 s (multi-MB transfers); their progress screen already cancels
on Back/Home.

Not covered by that bound:

- Blocking DNS/TCP connect inside the SDK cannot be interrupted from the fork
  (tracked separately as `wksfsg7xyt0zgbs586go86g7`).
- The SDK's implicit HTML login is outside the fork transport (up to ~10 x 30 s).
- While a synchronous fetch owns the loop, only the physical Back and Home keys
  are polled; the on-screen header back arrow is not.

Hardware acceptance (owner):

1. Run `python3 scripts/opds_failure_server.py --port 8099` on a LAN host and
   note the printed address.
2. Add `http://<host>:8099/` as an OPDS server. The feed must load and list
   three subsection entries and one book.
3. `http://<host>:8099/stall` (server accepts, sends nothing): an error screen
   must appear within ~10–20 s; Retry starts another bounded attempt; Back
   returns Home.
4. `http://<host>:8099/slow` (bytes trickle forever): the error screen must
   appear at the 20 s deadline; before this change this case hung until reset.
5. `http://<host>:8099/loop` (redirects to itself): an ordinary failure after
   five redirects; Retry and Back work.
6. During steps 3–5 press Back mid-fetch: immediate return to Home, no restart,
   no crash. Re-enter the catalog several times to check for leaks.
7. Real case: open `flibusta.is/opds`. It must end in the feed or a clear error
   within the bound; reading position and saved state must be untouched.

Host checks in this branch: `CatalogFetchLimiterTest` (8 cases) and the full
CTest suite. Hardware behaviour and live flibusta.is reachability are not
verified here.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
