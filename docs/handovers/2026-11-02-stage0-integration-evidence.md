# YBX Reader Stage 0 — integration evidence (2026-11-02)

## Scope

This is a status/evidence note for the external-source porting plan and Kaneo firmware #15/#39. It is **not** a firmware release or a declaration of hardware readiness.

## Confirmed

- Local firmware baseline `integrate/ybx-all-firmware-20261029` at `979386ee` contains 19 integration-only commits compared with `origin/feat/ybx-native-integration` at `20c5d6e4`. The published integration branch contains three plugin commits absent from the local integration branch.
- Created a clean isolated worktree `~/Projects/Ybx/.worktrees/ybx-stage0-integration-20261102` and local branch `integrate/ybx-stage0-20261102` from `979386ee`.
- Applied the three newer bundled-plugin commits in order:
  - `1cd29e92` -> `c5584b62`, adds plugin KOSync settings/read-back protections and sleep-mode UX;
  - `69825c9f` -> `ae75e155`, YBX icon;
  - `20c5d6e4` -> `85b34b72`, bundled plugin refresh.
- Reconciled the three Rate Cloud handover documentation commits `6bd701a2`, `71926830`, `ec4157c8` as `aa817021`, `1c97a48f`, `3518460b`. These contain handover evidence, not additional firmware changes.
- The bundled plugin remains **1.9.0** (its YBX native summary/awards API descriptors remain present); cherry-picking the older plugin commits did not downgrade the version in `plugin.json`.
- `freeink-sdk` submodule checked out at the candidate-pinned `44427f8d`.
- CMake host suite built and ran: **1091 / 1091 tests passed**, total ~31 s.
- `node --check sd-card/plugins/ybx/plugin.js`: passed.
- Built-in YBX plugin installer Python tests: **6 / 6 passed**. They verify existing credentials/config preservation, safe install and symlink checks.
- Confirmed `/api/files` exposes `isDirectory`, required by the updated plugin file-existence check.

## Unconfirmed / hold

- PlatformIO target builds `default` (ESP32-C3) and `x4pro` (ESP32-S3) were started on MacBook-Air-Yaraslau.local. The Desktop Commander connection stopped responding during initial dependency/SDK build. **No build PASS or FAIL is claimed.** Verify logs `/tmp/ybx-stage0-pio-c3.log` and `/tmp/ybx-stage0-pio-x4pro.log`, then rerun if incomplete.
- The consolidated worktree branch is **local only** as of writing. It was not pushed as a PR, because build verification and access to the authorized Mac were interrupted.
- Existing hardware acceptance on X3/X4/X4 Pro remains unverified. No OTA/production flash or deployment was made.
- Rate Cloud separate agent implementations are only audit candidates until separately verified/merged; their source worktrees were not blindly merged into this candidate.

## Next safe actions

1. Restore access to the Mac; check local branch status and finished/failed firmware build logs.
2. Validate the 1.9.0 plugin contract and all reported file differences; rerun C3 and S3 builds, resource-size budgets, and dedicated TLS/sleep-event regressions.
3. Run `python3 scripts/ybx_upstream_sync.py --check-inventory` on the exact candidate and update the inventory if needed.
4. Push `integrate/ybx-stage0-20261102` and open a **draft** PR against `feat/ybx-native-integration` only once the candidate's state and tests are verified; do **not** merge or release automatically.
5. Continue P0 network/TLS/sleep gates, then crash-safe SD persistence work as separate reviewable PRs.
