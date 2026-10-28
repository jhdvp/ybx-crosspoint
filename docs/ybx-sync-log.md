# Upstream sync log

One entry per upstream integration. `scripts/ybx_upstream_sync.py --record
docs/ybx-sync-log.md` appends the entry; the merge commit message stays the
authoritative record. This file answers two questions for whoever syncs next:
which upstream commit is already inside the fork, and what was still open.

## 2026-10-28 — upstream `31dc683f9`

- Integrated upstream SHA: `31dc683f9` (`fix: broken focus reading with RTL text (#4230)`).
- Merge commit: `a8e0bbba` (2026-10-28), `crosspoint-reader/develop` into the YBX branch.
- Previous recorded base: `a790b1449`.
- Conflicts: not recorded at merge time. The three upstream fixes carried in the
  fork branch (`40a633e7` reader menu selection, `3f8345c5` toolbar padding,
  `31dc683f` focus reading with RTL text) became upstream-owned with this merge and
  left the fork diff, including `lib/Epub/**`, `src/activities/reader/**` and the
  `SECTION_FILE_VERSION` 70→71 bump.
- Verification: host build and suites as recorded in the merge; device acceptance
  still owed by the maintainer (see [validation status](ybx-validation-status.md)).
- Open: none against this SHA. `upstream/develop` is an ancestor of HEAD.

## 2026-10-28 — procedure validation (no upstream integration)

- `scripts/ybx_upstream_sync.py` and [the fork inventory](ybx-fork-inventory.md)
  were added and exercised against `upstream/develop` at `31dc683f9` and against a
  throwaway simulated upstream branch that touched the YBX-owned lines in
  `platformio.ini`, `src/network/OtaUpdater.cpp`, `.github/workflows/release.yml`,
  `lib/I18n/translations/english.yaml`, `src/activities/home/HomeActivity.cpp` and
  `test/CMakeLists.txt`.
- Simulated result: 3 conflicted paths, all classified by area; the invariant check
  correctly failed on the release matrix (`9 device: rows, expected at most 1`).
  The real merge ran on a throwaway sync branch, was left uncommitted and was
  aborted; the throwaway branches were deleted.
- Baseline checks on the fork tree, run through the script's own `--build` step:
  `pio run -e default` SUCCESS (RAM 66,712/327,680 bytes, flash 5,544,689/6,553,600
  bytes), `pio run -t unit-tests` SUCCESS (898/898 tests passed). Logs stayed in the
  ignored `.ybx-sync.local/` directory; they are not committed.
- Environment note: a first build in a fresh worktree downloads the PlatformIO
  package set. With several worktrees building at once this stall can look like a
  hang; `pio pkg install -e default` first, then the procedure's `--build` step.
- No fork commit resulted from this entry.

## 2026-10-28 — observed upstream advance, not integrated

- New upstream commit: `8f4d035c` (`fix: Move Picco switch to hardware setting
  not software switch (#4234)`), 7 paths.
- Integrated upstream SHA stays `31dc683f9`; this advance is deliberately not
  merged here.
- Simulation against `upstream/develop`: 0 conflicts, all YBX invariants OK on the
  resulting tree. One both-side churn path: `src/main.cpp` (area `sleep-events`),
  where the fork changed `deliverSleepPluginEvents()` and upstream changed the
  frontlight setting. Clean auto-merge here is expected, not proof: re-read both
  hunks when integrating.
- Owner next step: `python3 scripts/ybx_upstream_sync.py --merge`, then
  `--no-fetch --merge --build`, then commit and `--record docs/ybx-sync-log.md`.
