# YBX implementation status — 28 October 2026

This branch saves ongoing implementation work. Firmware handoff is incomplete;
these checks do not establish release readiness or hardware support acceptance.
The maintainer explicitly requested committing and pushing the current state
with the outstanding limitations already reported. This does not record design
acceptance or waive the remaining release checks.

## Implemented

- Compact X3/X4 Home using the existing UI host, local recent books, saved
  progress and a Library / Catalogs / More row. No account statistics are invented.
- Native achievements screen behind Home > More > Achievements: received and
  pending Yandex rewards from the shared account, token-bound SD cache, explicit
  no-data/offline/expired/unlinked states, sign-in handoff to the YBX plugin.
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
- Achievements parser suite: **9/9 passed** (documented and flat response
  shapes, caps/truncation, malformed bodies, UTF-8 truncation, timestamps).
  The run reports 905/907 because two untouched
  `FlowBoundaries/ParserStopTest.PageCallbackStopsParsingAndClosesFile` cases in
  `lib/Epub`/`test/chapter_html_slim_parser` fail on this baseline.
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
- Native account statistics are planned, not implemented.
- Achievements are implemented at source level only (see the checklist below):
  the service sends no reward image a cp token can fetch, so rows are text; the
  endpoint offers one limited page per section (limit only, no offset), so at
  most 20 received and 20 pending awards are shown, with a note when more exist;
  the transport/cache paths have no host tests because they need SD and HTTP.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- Inherited HTTPS certificate verification remains disabled. Transport/OTA
  trust hardening is a separate release blocker.
- No device flashing, hardware validation, release or PR is part of these checks.

## Achievements screen — owner acceptance checklist (2026-10-28)

Entry: Home > More > Achievements (X3/X4 compact Home). Sign in through the YBX
plugin first; the screen reads the same plugin token and account rewards as
Classic and Desktop and adds no second rewards system.

- Received: earned rewards show title, description and `earned <date>`. A card
  with `earned_at: null` shows `seen <date>` (first observation), never a
  fabricated earned date.
- Pending: in-progress rows show the server description and, when supplied,
  `%` or `n of m` in the value slot; absent progress must stay blank, not `0%`.
- Same set: compare titles with Classic history-awards and Desktop Awards for
  the same account. The device shows the first 20 of each section (`limit=20`)
  and a `Showing the first N` note when the server reports more.
- Refresh: the Refresh row asks for Wi-Fi when needed, paints `Updating...`
  before the request, then `Updated <time>`; refreshing twice must not duplicate
  or drop rows.
- Offline: with a saved copy, a failed refresh shows `Offline` plus `Saved <time>`
  and keeps the rows. With no saved copy it says no copy is saved.
- No data is not "no rewards": a first run without a cache shows `Not loaded yet`
  and no award rows; an account that genuinely has none shows
  `No achievements yet`.
- Expired/unlinked: a rejected token shows `Session expired` and tapping opens
  the YBX plugin sign-in; an unlinked device shows `Yandex account not linked`.
  Both must re-check after linking.
- Account switch: sign out and in as another YBX account; the previous account's
  saved copy must not appear (the cache is bound to the token fingerprint).
- Images: not shown. A cp token cannot fetch the device-scoped reward image URL,
  so every reward must remain readable as title/description text.
- Memory/leaving: enter, refresh and exit several times; the snapshot is one
  ~10 KB heap object plus the transient HTTP body, and Home must return without
  heap growth.
- Long translated titles (Russian at least) wrap in both orientations.

Record the firmware version and account used. Do not mark rewards working from
the menu entry alone.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
