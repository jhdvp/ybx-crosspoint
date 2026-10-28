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

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

## On-reader shelf parity — 28 October 2026 (this branch, unreviewed)

Code presence only; no device run was performed. The change adds, in
PluginCatalogActivity, HomeActivity, CatalogActivity and PluginHttp:

- "На ридере" row/detail state when the planned download destination already
  exists; the destination is computed by the same code the downloader uses
  (`pluginhttp::pickDownloadDir`, shared with the new non-UI
  `pluginhttp::catalogDownloadDir`).
- Tap/Confirm on such a row opens the book (silent reboot into the reader, the
  established LCP-loan pattern) instead of downloading again.
- DONE screen after a download: Confirm opens the just-downloaded book; Back
  returns to the catalog.
- Long-press on an on-shelf row: "Скачать заново / Убрать с полки / Отмена".
  Removal is behind a confirmation dialog and deletes only the book file plus
  its rebuildable caches and its recent-books entry (shared `removeBookFile`
  primitive); settings, tokens, sidecar files of others and `/.crosspoint/`
  are never touched.
- YBX home More popup gains a first "На ридере" entry (only when the YBX
  plugin's download folder resolves): an offline FileBrowser rooted at
  `/books/ybx` — open, rename, delete and progress there are the existing
  FileBrowser behavior.
- Finished `.epub` downloads are checked for ZIP magic (rejected as "Сервер
  прислал не файл книги", file removed); storage write failures now report a
  distinct "нет места/ошибка записи" message in every catalog.

### Host verification

- `pio run -e default` (X4 profile): passes; RAM 66,712/327,680 bytes, Flash
  5,558,075/6,553,600 bytes.
- Host suite (CMake/CTest under `test/`): 898/898 pass. No suite drives
  PluginCatalogActivity or HomeActivity UI; these checks only prove the shared
  primitives (protected paths, safe removal, filename planning, i18n keys)
  still hold.
- Everything below is unverified until run on hardware.

### Owner acceptance checklist (device)

1. Catalog (YBX) → download a book → progress → "Книга скачана" → OK → reader
   opens that book at page 1; Back from the reader behaves normally.
2. Re-enter the catalog: the row reads "На ридере"; Confirm opens the book
   without network transfer; the footer button label is "Открыть".
3. Long-press the row → "Скачать заново" re-downloads and overwrites without
   extra prompts; → "Убрать с полки" shows the confirm with the book title;
   confirm removes only that file (other books, settings, login intact); the
   row returns to normal download state.
4. Home → More → "На ридере" (offline, airplane Wi-Fi off): folder of
   downloaded books opens; open/rename/delete work; Back returns home. With the
   plugin absent or Plugin System disabled, the entry is not shown.
5. Same book from two providers → two files (filenames carry ids) → both rows
   show "На ридере".
6. SD full or unplug mid-download → "Ошибка записи"/failure, no `.part`
   leftovers on the next attempt, retry works.
7. Server fault returning non-EPUB body → "Сервер прислал не файл книги", no
   file left in `/books/ybx`.
8. WB image-only book opens with cover; all four rotations (portrait, inverted,
   landscape CW/CCW) render rows, popup, DONE screen and shelf entries.
9. Repeat 1–4 twice to watch heap-driven behavior (auto-reboot is silent; no
   crash/reboot loops while browsing).
