# Firmware consolidation — 12 November 2026

One main branch, one lineage. This records what the branch is, what every old
branch and worktree was, and the exact commands to finish the cleanup.

Companion evidence: `reports/fw-survey/REPORT.md` and its independent
`VERIFY.md` (both outside the repository, under
`~/.local/state/ybx-pocket-agents/reports/fw-survey/`). Where this document and
the survey disagree, this document wins: it was written after the work landed
and its claims were re-measured.

## 1. The single main branch

**`ybx/main-20261112`** is the intended main. Tip at the time of writing:
`616e9c62`.

It is the port line (`port/ybx-unified-on-2rc-20261105`, itself upstream
`2.0.0rc` + the nested-catalog PR #10 + 46 port commits) **plus**:

| Commit | Content |
| --- | --- |
| `a42607f1` | merge of upstream `2.0.0` (60 commits), resolved file by file |
| `f402bf15` | sync note recording the then-reverted develop merge |
| `992a8257` | SDK pin `44427f8d` → `5e94fe74` (upstream 2.0.0's pin), both SecureNet stages re-ported, digests regenerated |
| `b53a6d87` | SDK pin → `6a208c0c` (develop's pin), needed by `SDCardManager::sdUsedBytes(uint64_t&)` |
| `9f3236d6` | merge of `upstream/develop` (48 commits) from the saved resolutions, plus two repairs |
| `18b8abd3` | crash-safe `PersistableStore` persistence + its 9-case host suite (the redundant extra temp layer was removed after review — see §2.1) |
| `c2c5fdbc` | PR #29: monthly reading-history archive, `My Clippings.txt` hardening, export-failure logging |
| `ee94cf7c` | OTA minimum-version floor (`2.0.0`) wired into `isUpdateNewer()`, per-cause failure taxonomy |
| `b0c9e394` | shelf removal gated on firmware-owned provenance records instead of a plantable `.downloaded` marker |
| `266e65d0` | YBX logo + generator, YBX README/user guide, upstream README preserved as `README-UPSTREAM.md` |
| `255c6ea4` | `tools/` host devtools: X4 Pro QEMU system emulation and native sim (142 files, zero firmware delta) |
| `616e9c62` | native YBX server mirrors (Auto/Main/Europe/Portugal) and KOSync `BINARY` default |

The old GitHub default `feat/ybx-native-integration` is **behind** this branch,
not ahead of it: it has no `TlsPolicy`, no `TlsRoots`, no `TlsTransport`, no
`scripts/patch_securenet.py` and no `test/tls_policy`. Moving main here loses no
product feature.

### Two defects found in the saved develop resolutions

The recorded merge 2 had only ever been checked with the host suite, so two
device-build defects survived in it. Both are fixed in `9f3236d6`:

- `HomeActivity.cpp` called the fork's `hasLibrarySlot()`, which the resolved
  header no longer declares (upstream replaced it with `HomeMenuItem slotItem()`).
  Now `slotItem() != HomeMenuItem::NONE` — the same predicate.
- `requestStream()` is new in develop and called the fork's `openClient()`
  without the `timeoutMs` argument. It now takes the fork's usual trailing
  `uint32_t timeoutMs = 30000` and forwards it.

### One survey claim that did not reproduce

`VERIFY.md` §1.4 called the clippings-export ternary at
`EpubReaderActivity.cpp:633` inverted and "a live user-visible bug". It is not:
the port already read `exported ? SAVED : Added ? EXPORT_FAILED : FAILED`, which
is equivalent to PR #29's ordering because `exported` can only be true when the
clipping was added. PR #29 was still ported — it makes the intent explicit and
adds the missing `LOG_ERR` — but no inversion was fixed, because none existed.

## 2. Verification

| Check | Result |
| --- | --- |
| `pio run -e default` | SUCCESS — RAM 20.4 %, flash 88.9 % |
| `pio run -e x4pro` | SUCCESS — RAM 34.1 %, flash 86.3 % |
| Host suite (`cmake --build build/test` + `ctest`) | **1349 / 1349** |
| SDK SecureNet harness (`freeink-sdk/libs/network/SecureNet/test/host/run.sh`) | resumable fetch, bulk request, deadline DNS — all pass |
| `scripts/patch_securenet.py` | applies both stages to a pristine `6a208c0c`, idempotent on a second run |
| Logo generator | `scripts/generate_ybx_logo.py` reproduces the checked-in PNG byte for byte |

Build logs are in `.build-logs/` (gitignored).

**Not done, and not claimed:**

- **No hardware testing at all.** TLS trust against real servers, QR pairing,
  clippings sync, the shelf, sleep art, the new five-button account row on each
  panel size, and the SDK's display/touch/power changes are the owner's
  acceptance work.
- `bin/clang-format-fix` could not run: no clang-format on this host. Edited
  lines were kept inside the 120-column limit by hand.
- The SDK pin advance compiles upstream's board fixes (Picco power saving and
  frontlight, eego A4 timing, SDMMC 40 MHz, GT911 touch, `.notdef` glyphs,
  bounded JSON previews) but does not device-test them.
- The mirror's five-minute probe behaviour on a dropping network is covered by
  host tests of the policy, not by a device on a real network.
- No dedicated host test for the KOSync `BINARY` default: the store needs an
  MD5/base64/obfuscation stub harness that does not exist yet. The change is
  covered by inspection and by the YBX provisioning path, which already wrote
  `BINARY`.

## 2.1 Independent review and the fixes it produced

Four read-only reviewers (correctness, architecture, embedded resources,
i18n/docs) inspected the diff. Their verified findings were fixed as one batch;
both firmware profiles and the 1349-test host suite were re-run green afterwards.

| Finding | Fix |
| --- | --- |
| **The rescued `PersistableStore` layer was redundant.** `SDCardManager::writeFile()` already writes `path.tmp` and swaps it in — at `44427f8d` too. The extra layer produced `path.tmp.tmp`, doubling every save's SD writes and adding a second remove/rename window without adding a guarantee. | Removed the layer; `writeDocToFile()` calls `Storage.writeFile()` once. The host stub now models the real HAL contract and the 9 cases test that, instead of a guarantee the device never had. The header states the remaining remove-then-rename window honestly. |
| **A mirror retry could re-send a streamed response.** `requestStreamOnce` flagged any `status < 0` as a connection failure, including a mid-body drop after `onStart` had already sent `200` + headers to the browser. The retry then sent a second response. The same shape could replay a POST. | `connectionFailure` now means only "the connect failed" (`openClient()` returned null). A lost answer mid-request stays on the host that produced it. |
| **Three candidates each got the full timeout**, so a dead YBX host froze the main task for 90 s instead of 30 s. | One absolute deadline is shared across the sequence; each attempt gets the remaining budget. |
| **An abort or a spent budget was recorded as "every host failed"**, pinning a mirror for five minutes without evidence. | `allFailed()` only runs when the loop exhausted its candidates. |
| **Shelf provenance had no upgrade path**: books downloaded by the marker-era firmware lost On-Reader status and their markers were orphaned. | `computeOnReaderStatus()` adopts an existing `.downloaded` marker into a protected record once, then removes the marker. |
| **A transient open failure deleted a provenance record**, making a book permanently unremovable. | The record is only cleared when the path is really gone (`Storage.exists`). |
| **The KOSync default change could flip very old configs.** A config written before the key existed would silently move from filename to content-hash ids. | `CONFIG_VERSION` bumped to 4; a pre-v4 config with no stored method keeps `FILENAME`. Fresh setups get `BINARY`. |
| **The first SD read of the server choice happened on the render task under the render lock.** | The mode is read in `onEnter()` and cached; `draw()` only formats the cached label. |
| `Plan` parsed the caller's string, not its own copy; `const` mutators hid shared-state writes; the single-attempt helpers had external linkage. | Parse `url_`; dropped `const`; made the helpers `static`. |
| Stale/incorrect docs: README test count, guide language count, the parity doc's claim that KOSync goes through the mirror, a historical SDK-pin line, a link to an unpushed branch. | All corrected. The parity doc now states the KOSync gap explicitly. |

Findings deliberately **not** acted on, with reasons:

- **`opds.*` / `sync.*` families are inert today.** Only `my.ybx.app` is
  requested by native firmware, and `KOReaderSyncClient` bypasses `pluginhttp`.
  The families are kept because the owner asked for the plugin 1.6.17 host map;
  wiring the KOSync and OPDS clients onto them is recorded as a follow-up.
- **The monthly reading archive has no firmware consumer.** `archive::daySeconds`
  is called only from tests. That is PR #29's own shape, ported as asked; adding
  a reader UI for it is a separate feature.
- **The mirror preference table is process-global and unsynchronised.** All
  current callers run on the main task; the worst case is a lost preference
  update, not corruption. Revisit if a request path moves to another task.
- **The fifth account button shrinks the row ~22 %.** Geometry is derived from
  the safe area and the label wraps to two lines; whether it fits each panel is
  device acceptance, not a code change.

## 3. Archive tags — the safety net

**44 local tags**, all named `archive/<name>-20261112`. They are local only;
nothing was pushed. Every branch tip that is neither an ancestor of main nor
recoverable from `upstream/*` is contained in at least one of them, so no
deletion below can destroy an object.

The ones the owner asked for first:

| Tag | Tip | What it saves |
| --- | --- | --- |
| `archive/ybx-emu-batch2-review-20261112` | `0154bc2c` | the whole emulation family supertip; was on **no remote at all** |
| `archive/ybx-qwen38-batch2-{browser-resilience,epd-lifecycle,gui-navigation,repro-build,wifi-flow,wifi-radio}-20261112` | `e2d18071`, `8bccf3bc`, `9acea7c0`, `18ab05f8`, `7f218b5c`, `3aadb8a3` | the six batch2 lanes. **They are siblings of `0154bc2c`, not ancestors** (VERIFY.md W1), so the single emu tag does not cover their commit objects |
| `archive/ratecloud-rc-fw-01-20261112` | `979386ee` | the rc-fw-01 branch tip |
| `archive/rc-fw-01-persistable-store-20261112` | `a367a8f2` | the rc-fw-01 **uncommitted** work, committed onto the new branch `rescue/rc-fw-01-persistable-store-20261112` |

The rescue commit was built with git plumbing from the files in
`~/Projects/Ybx/artifacts/ratecloud-firmware-worktrees/rc-fw-01`; that worktree
was left exactly as it was found. Its `PersistableStore` writer had never been
compiled — the stub referenced `failAllWrites` unqualified — and that one-line
fix is part of `18b8abd3`.

The remaining tags cover the `agents/fw-*` family (13), `backup-fw-tls-wip`,
`feat/ybx-native-dashboard-20261029`, `review/ybx-achievements-20261029`, and
every fork-only remote tip: lineage A itself
(`archive/feat-ybx-native-integration-20261112`), PR #23 (`afcee36b`, preserved
by `archive/feat-ybx-native-account-qr-20261104-20261112`, whose tip descends
from it), PR #29, PR #28's docs branch, the five `ratecloud/rc-fw-*` branches,
and the per-feature YBX branches.

List them with `git tag -l 'archive/*' --format='%(refname:short) %(objectname:short)'`.

## 4. Remote branch disposition

`origin` = `jhdvp/ybx-crosspoint`, `upstream` = `crosspoint-reader/crosspoint-reader`.

### Keep (3)

| Branch | Tip | Why |
| --- | --- | --- |
| `origin/ybx/2.0.0rc` | `c4816777` | the port base, also tag `ybx-catalog-lists-rc.1`; an ancestor of main |
| `origin/port/ybx-unified-on-2rc-20261105` | `528ea1c0` | the previous main candidate; an ancestor of main. Keep until the cutover is announced, then delete |
| `origin/develop` | `72ca92fe` | lineage-A merge base reference; an ancestor of main. Keep until cutover, then delete |

### Delete — upstream mirrors, zero YBX content (21)

Verified by counting `src/activities/ybx`, `src/util/Ybx*`, `src/nativeapps`,
`sd-card/plugins/ybx` at each tip: all 0. All are recoverable from `upstream/*`
or from a tag on a branch that contains them.

`jpegdec-fork`, `feat-touch`, `feat-sd-theme-system`, `feat-aa-fonts`,
`feat-epub-parser-refactor`, `feature/book-page-counts`,
`fix/handle-crashes-on-very-large-epub-chapters-#2256`, `feat-chunk-bitmaps`,
`feat/recursive-dict-lookup`, `fix/word-spacing-string-cleanup`,
`feature/reader-tap-maps`, `feat-settings-consolidation`,
`feature/crossink-controls-port`, `master`, `refactor/image-pipeline`,
`feat/swipe-hints`, `feat-embedded-fonts`, `feat/hypen-manager`,
`feat-font-compression-tables`, `chore/license-update`,
`chore/update-translations`.

(`master` is not an ancestor of main, but its tip is contained in tagged
branches, so it is preserved.)

### Delete — YBX feature branches whose content is in main (26)

Content-verified by the survey and its independent check, not by ancestry: the
port was file-by-file, so these are mostly **not** ancestors of main even though
every path they added is present. All are archive-tagged.

`feat/ybx-native-integration` (lineage A root — branding and user guide taken
in `266e65d0`), `integrate/ybx-unified-20261104` (PR #23, 254/254 paths present),
`feat/ybx-crossink-picks-20261107` (PR #29, ported in `c2c5fdbc`),
`docs/readme-refresh-20261107` (PR #28, ported in `266e65d0`),
`feat/ybx-native-apps-20261103`, `feat/ybx-notes-20261103`,
`feat/ybx-markdown-20261103`, `fix/ybx-markdown-progress-migration-20261103`,
`feat/ybx-native-plugin-191-20261103`, `fix/ybx-readingstats-dedup-20261102`,
`fix/ybx-stats-atomic-20261103`, `fix/ybx-nosd-recovery-20261103`,
`fix/ybx-clippings-identity-tls-20261104`,
`fix/ybx-native-note-document-id-20261104`,
`feat/ybx-note-context-anchors-20261104`, `feat/ybx-note-anchor-sync-v1-20261104`,
`feat/ybx-native-note-unique-reanchor-20261104`,
`perf/ybx-reader-progress-write-gate-20261103`, `integrate/ybx-stage0-20261102`,
`integrate/ybx-reader-review-20261029`, `feat/ybx-native-account-qr-20261104`,
`feat/ybx-qr-clippings-autosync-20261104`,
`fix/ybx-sleep-companion-grayscale-20261104`,
`fix/ybx-authenticated-shelves-20261103`, `handoff/ratecloud-20261102`,
`docs/ybx-external-ports-20261102`.

Note the ref name: it is `feat/ybx-native-note-unique-reanchor-20261104`, not
the `feat/ybx-note-unique-reanchor-20261104` the survey's §6.2 table prints
(VERIFY.md W4). A cleanup script built from that table would silently skip it.

### Delete — superseded prototypes and experiments (9)

`review/ybx-native-account-20261029` (early account prototype; the shipped
design is `YbxNativeSession` + `YbxNativeAuthPolicy` + the compiled-in manifest),
`design/ybx-account-ui-20261029` (its 3 un-pushed local gallery commits are
ancestors of `0154bc2c`, so the emu tag preserves them),
`feat/ybx-esp32-full-system-emulation-20261029`,
`feat/ybx-native-freeink-simulator-20261029`,
`fix/ybx-qemu-boot-evidence-20261029`,
`agents/ratecloud/rc-fw-03`, `agents/ratecloud/rc-fw-06`,
`agents/ratecloud/rc-fw-09`, `agents/ratecloud/rc-fw-10`.

### Delete after the owner decides the open items (1)

`agents/ratecloud/rc-fw-07` — its OTA floor is now in main (`ee94cf7c`), but its
branch-local `reports/ratecloud/rc-fw-07.md` note exists nowhere else. It is
tagged (`archive/agents-ratecloud-rc-fw-07-20261112`), so deletion is safe.

## 5. Worktree disposition

66 registered worktrees, 2 prunable (`/private/tmp/tls-baseline`,
`/private/tmp/tls-baseline-deadline` — directories already gone).

Only these carry content beyond the `M freeink-sdk` submodule noise:

| Worktree | Content | Disposition |
| --- | --- | --- |
| `~/Projects/Ybx/artifacts/ratecloud-firmware-worktrees/rc-fw-01` | the atomic `PersistableStore` writer + `test/persistable_store/` | **saved** as `a367a8f2` and landed in `18b8abd3`. Left untouched; prune when ready |
| `~/.local/state/xteink-agent-workspaces/fw-qemu-evidence-20261029` | modified `tools/ybx-system-emu/{QEMU_BRINGUP.md,README.md}`, blobs in no ref | **still uncommitted.** The `tools/` import took the committed versions, not these edits. Commit or copy them before pruning |
| `~/.local/state/xteink-agent-workspaces/ybx-qwen38-six-20261030/{6 lanes}` | 43 untracked/modified `experiments/` files; 4 blobs committed nowhere (`0037-agent-render-pipeline.md`, the 366-line `README.md` variant in `crosscore-ipi`, `probe_x4pro_guest_touch_ui.py` and its test in `acceptance-probe`) | **still uncommitted.** Host devtools only, no firmware delta. Archive before pruning |
| `~/.local/state/xteink-agent-workspaces/fw-native-account-review-20261029` | 5 untracked files, 382 LOC: early `YbxAccountActivity`/`YbxAccountSources` prototype | superseded; archive if desired, do not port |
| `~/Projects/YBX/.worktrees/fw-main-20261112` | this work | becomes the main checkout |

Everything else is clean or shows only `M freeink-sdk`, which is the applied
SecureNet series and is **never** meant to be committed (see
`docs/ybx-sdk-patches.md`).

The survey's §7 path `ybx-qemu-evidence-20261029` is wrong; the directory is
`fw-qemu-evidence-20261029` (VERIFY.md W5).

## 6. Commands for the owner

Nothing below was run by the agent. **No push, no remote deletion, no branch or
worktree deletion was performed.** Run these yourself, in this order.

### 6.0 Preconditions

```sh
cd ~/Projects/YBX/CrossPoint-firmware
git fetch origin --prune
git fetch upstream --prune

# Confirm the archive tags exist locally (44 of them).
git tag -l 'archive/*' | wc -l

# Push the tags FIRST if you want the rescue preserved off this machine.
# This is the only step that publishes anything; skip it to keep tags local.
git push origin --tags
```

### 6.1 Make the new main the default

```sh
git push origin ybx/main-20261112
```

Then set `ybx/main-20261112` as the default branch in GitHub → Settings →
Branches. Do this **before** deleting `feat/ybx-native-integration`, which is
the current default.

### 6.2 Delete the upstream mirrors (zero YBX content)

```sh
git push origin --delete \
  jpegdec-fork feat-touch feat-sd-theme-system feat-aa-fonts \
  feat-epub-parser-refactor feature/book-page-counts \
  'fix/handle-crashes-on-very-large-epub-chapters-#2256' \
  feat-chunk-bitmaps feat/recursive-dict-lookup fix/word-spacing-string-cleanup \
  feature/reader-tap-maps feat-settings-consolidation \
  feature/crossink-controls-port master refactor/image-pipeline \
  feat/swipe-hints feat-embedded-fonts feat/hypen-manager \
  feat-font-compression-tables chore/license-update chore/update-translations
```

### 6.3 Delete the YBX feature branches whose content is in main

```sh
git push origin --delete \
  feat/ybx-native-integration \
  integrate/ybx-unified-20261104 \
  feat/ybx-crossink-picks-20261107 \
  docs/readme-refresh-20261107 \
  feat/ybx-native-apps-20261103 feat/ybx-notes-20261103 feat/ybx-markdown-20261103 \
  fix/ybx-markdown-progress-migration-20261103 feat/ybx-native-plugin-191-20261103 \
  fix/ybx-readingstats-dedup-20261102 fix/ybx-stats-atomic-20261103 \
  fix/ybx-nosd-recovery-20261103 fix/ybx-clippings-identity-tls-20261104 \
  fix/ybx-native-note-document-id-20261104 feat/ybx-note-context-anchors-20261104 \
  feat/ybx-note-anchor-sync-v1-20261104 feat/ybx-native-note-unique-reanchor-20261104 \
  perf/ybx-reader-progress-write-gate-20261103 integrate/ybx-stage0-20261102 \
  integrate/ybx-reader-review-20261029 feat/ybx-native-account-qr-20261104 \
  feat/ybx-qr-clippings-autosync-20261104 fix/ybx-sleep-companion-grayscale-20261104 \
  fix/ybx-authenticated-shelves-20261103 handoff/ratecloud-20261102 \
  docs/ybx-external-ports-20261102
```

### 6.4 Delete the superseded prototypes, experiments and ratecloud branches

```sh
git push origin --delete \
  review/ybx-native-account-20261029 design/ybx-account-ui-20261029 \
  feat/ybx-esp32-full-system-emulation-20261029 \
  feat/ybx-native-freeink-simulator-20261029 fix/ybx-qemu-boot-evidence-20261029 \
  agents/ratecloud/rc-fw-03 agents/ratecloud/rc-fw-06 agents/ratecloud/rc-fw-07 \
  agents/ratecloud/rc-fw-09 agents/ratecloud/rc-fw-10
```

### 6.5 Delete the old main candidates, after the cutover is announced

```sh
git push origin --delete port/ybx-unified-on-2rc-20261105 develop
```

Keep `ybx/2.0.0rc` as the named port base, or delete it too — it is an ancestor
of main and is also tag `ybx-catalog-lists-rc.1`.

### 6.6 Local cleanup

```sh
# Every local-only tip is archive-tagged, so -D is safe. Verify first:
git tag -l 'archive/*' | wc -l    # expect 44

# Delete local branches that are fully contained in main.
git branch --merged ybx/main-20261112 \
  | grep -v -E '^\*|ybx/main-20261112|rescue/' \
  | xargs -r git branch -d

# The rest are local-only and tagged; -D is required and safe.
git branch --format='%(refname:short)' \
  | grep -v -E 'ybx/main-20261112|rescue/' \
  | xargs -r git branch -D

git remote prune origin
git worktree prune          # removes the 2 /private/tmp rows
```

Worktree directories are **not** removed by `git worktree prune` unless they are
already gone. Remove the rest by hand, after saving the two uncommitted sets
listed in §5:

```sh
# Example, one at a time, after confirming the worktree holds nothing unique:
git worktree remove ~/.local/state/xteink-agent-workspaces/fw-achievements-20261028
```

## 7. What is still open

Owner decisions, deliberately **not** taken by the agent (survey §5 P2/P3):

1. `test/test_package_release.py` (188 lines, `agents/fw-ota-release`): the
   release packager ships on main but its test does not. The release pipeline is
   untested.
2. Four host tests never carried: `test/sd_font_registry/*`,
   `test/test_confirmation_translations.py`, `test/test_text_settings_layout.py`.
   `sd_font_registry` overlaps the deferred EpdFont cluster — re-check now that
   develop is merged.
3. Deferred reconcile clusters: `lib/EpdFont` + `handleFontList`
   `family.filePath(...)`, `DetailsActivity` heading-marker/`pageProps`.
4. `test/firmware_board_tag_scanner/` (168 LOC, target source already on main,
   never registered in CMake).
5. `ReadingStatsFilesTest.cpp` (235 lines) — file-level coverage the checksummed
   snapshot suite does not replace.
6. Standalone `YbxAchievements*` screen: the capability is present as a view
   inside `YbxStatisticsActivity`; a separate screen is a preference, not a gap.
7. Account helper extraction (`pollOutcomeFromErrorCode`, `clampPollIntervalMs`):
   behaviour is inlined on main; only the unit tests would be new.
8. `flock`/`LOCK_EX` in `scripts/patch_securenet.py` (`agents/fw-sdk-dns` has a
   variant with 4 lock sites). **Choice recorded: not ported.** The script is a
   `pre:` hook run once per build; concurrent-run protection was left out to keep
   the re-ported series minimal. Revisit if parallel builds become normal.
9. `docs/handovers/2026-11-02-stage0-integration-evidence.md` (36 lines of
   provenance, on `docs/ybx-external-ports-20261102`).
10. The interactive screen gallery `docs/ybx-reader-visualization/` (14 files at
    `0154bc2c`) was **not** imported with `tools/`; it is preserved by the emu
    archive tag.
11. `PluginTokenAtomic.h` was **not** ported, and does not exist on main: the
    atomic-write decision was taken once, in favour of `PersistableStore`
    (`18b8abd3`). Note that `pluginhttp::saveTokenToFile()` still writes the
    plugin token with a direct `Storage.writeFile()`; routing it through the same
    swap is a small follow-up if the owner wants one mechanism everywhere.
12. Wire `KOReaderSyncClient` and the native OPDS path through the same mirror
    plan. They build their own `SecureHttpClient`, so today only the
    `my.ybx.app` family is actually mirrored; the `opds.*` and `sync.*` families
    are carried but inert.
13. Submit both SecureNet stages upstream to the FreeInk SDK. When released,
    delete the patch assets and script and bump the pin instead of carrying them
    (`docs/ybx-sdk-patches.md` §"Updating the SDK pin", step 4).
14. `test/test_release_license.py` arrived with develop but is not registered in
    `test/CMakeLists.txt` — upstream does not register it either.
15. Host coverage for the mirror retry wrappers themselves: the policy has 11
    cases, but the `connectionFailure` classification and the shared-deadline
    loop in `PluginHttp.cpp` are exercised only by inspection and the device
    build. A stub-transport suite would close that gap.

Roadmap packages explicitly **not implemented** in any branch (vCodex stats
import, EPUB content profiles, bilingual EPUB, saved articles/RSS, flashcards,
alternate-flash/USB, external board extras) remain out of scope; their status is
recorded in `docs/ybx-unified-integration-20261104.md`.