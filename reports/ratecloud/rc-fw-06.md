# rc-fw-06 — WIP: reading stats replay/dedup characterization

## Preserved work
- `test/reading_stats/ReadingStatsDedupeRegressionTest.cpp`: two characterization cases demonstrating that replay of older session IDs can inflate total seconds and per-book totals.
- `test/reading_stats/CMakeLists.txt`: adds cases to host target.

## Important caveat
These assertions intentionally expect CURRENT BROKEN behavior (`EXPECT_FALSE(isRecorded(oldId))`, `EXPECT_GT` inflated totals). They are not passing regression guards for the desired behavior. This commit DOES NOT fix the defect. Tests were not independently run in this handoff.

## Next developer
1. Reproduce on host and confirm the test target compiles.
2. Implement bounded persistent dedup across replayed session IDs, not merely `lastSessionId`; account for storage and backward-compatible migration.
3. Turn characterization tests into proper no-inflation assertions; check both totals and book records.
4. Run all reading_stats and journal/delivery tests on host; check overlap with active native stats worktrees.

Branch: `agents/ratecloud/rc-fw-06`. Not approved for merge.
