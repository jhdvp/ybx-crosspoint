# ReadingStats replay-id deduplication — isolated bugfix

**Base:** `integrate/ybx-stage0-20261102` (draft integration PR #7).
**Source:** Rate Cloud trial rc-fw-06 / original `agents/ratecloud/rc-fw-06` characterization. Its original WIP tests intentionally asserted broken replay behavior and are **not** considered an accepted fix.

## Defect

Before this patch, `src/util/ReadingStats.cpp::recordSession()` compared new event IDs only with `State.lastSessionId`. A previous event replayed after a newer event caused `addSession` and `updateBooks` to run twice. That inflated local totals, while the YBX Server already had an independent idempotency key.

## Change

- A `State` keeps the last **16** session event IDs in a fixed-size circular array, together with the existing last event ID.
- CPS2 state file (byte 4 = 2) preserves the 56-byte CPS1 header and day records, then appends a fixed 324-byte replay trailer. Every newly written state uses CPS2; reads accept both CPS1 and CPS2.
- On CPS1 read, the single legacy ID is migrated into the ring, preserving all existing totals/dated days.
- Malformed version, short tail, invalid count/cursor and missing terminators fail parsing; the prior valid file or transactional pending copy can still be loaded.
- New sessions with empty/oversized IDs are rejected before they modify totals; **no truncation-based aliasing** of two different event IDs.
- The model, state write and book write still share the original `recordSession()` path. There is no second reading timer or new account-level tracking.

## Acceptance and limits

Host regression tests cover replay of an older recent event, persistent round-trip, CPS1 migration, bounded eviction, invalid/truncated tail and mirrored book-gate behavior. Device-target compilation is a separate required gate.

**Important:** This is bounded replay protection, not permanent dedup for unlimited history. When a session ID falls out of the most recent 16, replay can still count it again. The existing state-save-then-book-save sequence is not one atomic two-file transaction: a failed per-book update after state persistence can still leave book totals behind aggregate totals. That second durability problem belongs to P1 crash-safe storage, not to this narrow fix.

Do not deploy or merge without review and real-device SD/power-loss tests. No backward-writing CPS1 compatibility is promised: older firmware may not read new CPS2 files. Recovery/downgrade must back up `/.crosspoint/reading-stats.bin` or explicitly migrate it before reverting firmware.
