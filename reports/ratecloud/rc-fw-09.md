# rc-fw-09 — WIP: reader text settings focus investigation

## Preserved work
`src/activities/settings/TextSettingsActivity.cpp` modifies `switchTab` local name from `onTabBar` to `wasOnTabBar` and comments on capturing state before changing tabs.

## Important caveat
Variable renaming and comments do not change the control flow or behavior. This is NOT a confirmed fix for a navigation/focus regression and no automated test is included. Mark as exploratory only.

## Next developer
1. Reproduce keyboard/rotary focus loss when moving between text-settings tabs.
2. Verify whether `ringPos()` before tab mutation is already correct (it was in baseline).
3. Add an actual failing test around restoration of selection after `rebuildRowItems()`.
4. Only retain this patch if it clarifies the root cause; otherwise drop it.

Branch: `agents/ratecloud/rc-fw-09`. Not approved for merge.
