# Rate Cloud firmware sprint — handoff, 2026-11-02

**Status: WIP / research only. No merge to main, no firmware flashing, deployment or release.**

## Overview

- Firmware repo: https://github.com/jhdvp/ybx-crosspoint
- Base local integration commit: `979386ee` (`integrate/ybx-all-firmware-20261029`).
- Shared trial capacity: **one simultaneous slot across all five Rate Cloud models**.
- Dispatcher: 2026-11-02 15:30–17:30 (+03:00). 10 of 12 tasks attempted; 1 counted as successful by the dispatcher. Several other timed-out tasks left useful local diffs.
- **Five WIP code branches pushed**: rc-fw-03, rc-fw-06, rc-fw-07, rc-fw-09, rc-fw-10.
- 24/24 OTA release policy tests independently rerun and passed in rc-fw-07, but guard is not wired into updater. No firmware/device build was independently validated.
- Detailed task-specific reports committed to `reports/ratecloud/rc-fw-NN.md` on each of the five code branches.

## Task-by-task recovery

| Task | State | Branch | Evidence and next action |
|---|---|---|---|
| rc-fw-01 | DONE | local branch `rescue/rc-fw-01-persistable-store-20261112` | The 15-minute timeout was recorded as "no code saved", but the atomic `PersistableStore` swap and its `test/persistable_store` suite were left uncommitted in the worktree. Rescued 2026-11-12 (local tag `archive/rc-fw-01-persistable-store-20261112`, not pushed). Review then found the SDK's `Storage.writeFile()` already does the temp-file swap, so the extra layer was removed as redundant; the suite now covers the HAL contract. |
| rc-fw-02 | BLOCKED | — | Tools operated inside npm package instead of firmware worktree; no code saved. **Next:** Re-run credential/logging audit with absolute paths |
| rc-fw-03 | WIP | [agents/ratecloud/rc-fw-03](https://github.com/jhdvp/ybx-crosspoint/tree/agents/ratecloud/rc-fw-03) | Commit a52e4b5d; stack-buffer HTTP JSON optimization; no firmware build. **Next:** Build X3/X4/X4Pro, JSON boundary tests, heap/stack measurement |
| rc-fw-04 | BLOCKED | — | Wrong OpenCode tool directory; no code saved. **Next:** Reproduce one offline/expired-login UI case |
| rc-fw-05 | BLOCKED | — | Build failed with Python 3.14/SCons FortranCommon; freeink-sdk submodule touched locally. **Next:** Repair toolchain in isolated environment; no submodule gitlink commit |
| rc-fw-06 | WIP | [agents/ratecloud/rc-fw-06](https://github.com/jhdvp/ybx-crosspoint/tree/agents/ratecloud/rc-fw-06) | Commit e93b694a; characterization tests EXPECT broken dedup behavior. **Next:** Fix persisted multi-ID dedup, invert expectations, execute tests |
| rc-fw-07 | WIP | [agents/ratecloud/rc-fw-07](https://github.com/jhdvp/ybx-crosspoint/tree/agents/ratecloud/rc-fw-07) | Commit 0ef8576b; version floor policy, 24/24 standalone GTests passed. **Next:** Wire policy into OtaUpdater, validate channels, firmware builds |
| rc-fw-08 | NOT_DONE | — | EPUB memory audit timed out, no code saved. **Next:** Test one bounded ZipFile input case |
| rc-fw-09 | WIP | [agents/ratecloud/rc-fw-09](https://github.com/jhdvp/ybx-crosspoint/tree/agents/ratecloud/rc-fw-09) | Commit dc1415b3; only variable rename/comment in switchTab, NOT functional fix. **Next:** Reproduce focus regression or drop change |
| rc-fw-10 | WIP | [agents/ratecloud/rc-fw-10](https://github.com/jhdvp/ybx-crosspoint/tree/agents/ratecloud/rc-fw-10) | Commit 5c32aa0e; scanner tests not registered in root CMake, not run. **Next:** Wire CMake, compile, test tag policy and variants |
| rc-fw-11 | NOT_STARTED | — | Time expired before first run. **Next:** Malformed/oversized server response regression |
| rc-fw-12 | NOT_STARTED | — | Time expired before first run. **Next:** Cross-branch acceptance audit, safe cherry-pick order |

## Safe continuation instructions for a developer/agent

1. `git fetch origin 'agents/ratecloud/*:refs/remotes/origin/agents/ratecloud/*'` and check out **one ticket per isolated worktree**.
2. Read that branch's `reports/ratecloud/rc-fw-NN.md` before changing any code. Do not regard WIP status as merge readiness.
3. Explicitly set absolute `workdir` / `cd <worktree>` for every OpenCode shell/file tool: some models began in the Desktop Commander npm module folder despite an existing worktree.
4. First run the ticket's host tests, establish expected versus actual behavior, then implement a narrow fix and rerun. On blocked tasks, save a report and intermediate commit before timing out.
5. Do not mix changes from other YBX firmware agents; compare their branches before cherry-picking. No automatic merge, push to main, firmware flash or deployment.

## Acceptance priorities

- **P1 — OTA (`rc-fw-07`)**: test floor policy alone is insufficient until called from `OtaUpdater`; verify stable/prerelease semantics and real board builds.
- **P1 — statistics (`rc-fw-06`)**: existing tests ASSERT replay inflation, so they document the bug rather than prevent it. Implement durable dedup and rewrite to assert no inflation.
- **P2 — heap (`rc-fw-03`)**: verify 512-byte `snprintf` output remains valid JSON at maximum fields; actual structs use action[24], args[192], result[192]. Measure stack and compile.
- **P2 — board tag (`rc-fw-10`)**: standalone CMakeLists is not connected to root `test/CMakeLists.txt`; test behavior and decide if untagged image acceptance is intended.
- **P3 — UI (`rc-fw-09`)**: patch is effectively a rename. Either add reproducible behavioral fix or discard during review.
- **P1 — security (`rc-fw-02`)**: not audited because wrong cwd; do not label secure until reassessed.

## Evidence and known blockers

- `rc-fw-07`: `test/ota_release_policy_standalone/build/OtaReleasePolicyStandaloneTest --gtest_brief=1` executed from that worktree: **24 tests passed**. Build artifact stays local; CMake project is committed.
- `rc-fw-05`: Python 3.14 / SCons error `ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'`; no complete firmware build.
- `rc-fw-06`: characterization code is unverified by handoff run; `rc-fw-10`: test sources uncompiled/unverified; `rc-fw-03`: only code review, not target/device profiling.
- Raw model tool logs may contain local environment details. Keep in private local `~/.local/state/ybx-ratecloud-sprint/serial-logs/`, **do not publish to public GitHub**.

## Kaneo

- [Rate Cloud sprint](http://127.0.0.1:5173/dashboard/workspace/kmuPv6G5D6zt1dhwTeYFuf6E7HPLmaJ2/project/hjc9ev0xss6432hamr88kau1/board)
- [Parent YBX Reader firmware board](http://127.0.0.1:5173/dashboard/workspace/kmuPv6G5D6zt1dhwTeYFuf6E7HPLmaJ2/project/m7bzuab46as9kqwxtwlqw38i/board): handoff umbrella card **#39** (`jiczhiokb1vnol8ovipx40x5`).
- Private on-Mac diagnostics archive: `~/.local/state/ybx-ratecloud-sprint/archive/ratecloud-2026-11-02-private-logs.tar.gz`, SHA-256 `b6d73856a1f20f4e4173b6a229cb9a45f0d41dacf7eafee658d7291b518e3198`. **Never publish this archive**.

## Lessons for next trial

- Dispatch serially for shared-one-slot plans; suppress background quota probes that consume the same slot.
- Split work into **4–5-minute atomic tasks**, commit/report at first checkpoint, and track separate `tests passed` versus `report exists` states.
- Avoid confusing exit code 0 with completed work or a timed-out agent with no changes: inspect local diffs and files before discarding.

## Kaneo board consolidation (2026-11-02 19:07 +03:00)

- **All 12 active tickets were moved in-place**, without copying or recreating cards, from the trial project into `YBX Reader · прошивка` (project `m7bzuab46as9kqwxtwlqw38i`).
- Task IDs, comments, links, priority and status were verified unchanged against pre-move snapshot. The new main-board numbers are **#40–#51**, mapping `rc-fw-01` → #40 through `rc-fw-12` → #51.
- Source project `hjc9ev0xss6432hamr88kau1` was archived, **not deleted**. Its one pre-existing archived duplicate #13 remains there for history.
- Main board handoff acceptance task **#39** (`jiczhiokb1vnol8ovipx40x5`) now links and prioritizes the transferred tickets.
- Full before/after snapshots and migration verification logs are private on the Mac in `~/.local/state/ybx-ratecloud-sprint/board-merge-20261102/`. The source archive is not a second active task queue.
