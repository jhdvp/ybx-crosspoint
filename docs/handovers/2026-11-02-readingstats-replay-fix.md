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

## Verified build evidence (2026-11-03 +03:00)

- Host ReadingStatsTest: **35/35 passed**, including new replay and migration regressions.
- ESP32-C3 (default) and ESP32-S3 (x4pro) firmware builds: **both PASS**, each from this isolated worktree.
- Compiling does not establish physical board, SD power-cut or OTA rollback acceptance.

| Target | RAM | Flash |
|---|---|---|
| default | used 66904 bytes from 327680 bytes | used 5676825 bytes from 6553600 bytes |
| x4pro | used 111956 bytes from 327680 bytes | used 5521510 bytes from 6553600 bytes |

- X4 Pro application binary: 5526768 bytes; SHA-256: 65afa8313f5a3320365421b861fc1afda87fb2d5eee15ddbc548dcbe84362f21.
- After the X4 Pro build, PlatformIO cleaned the C3 `.pio/build/default` directory. Its successful build metrics remain in the private `c3-build.log`, but the C3 binary is not retained; do not claim a hash for it. For future release-candidate builds, copy validated images into a content-addressed artifacts directory **immediately after each target**.
- The build-only measurements do not certify peak heap, largest allocation or reliable SD behavior.
