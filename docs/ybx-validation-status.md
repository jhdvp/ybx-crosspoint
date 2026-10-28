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

## On-reader shelf parity — 28 October 2026 (this branch, review fixes applied)

Code presence only; no device run was performed. The change adds, in
PluginCatalogActivity, HomeActivity, CatalogActivity, FileBrowserActivity,
PluginHttp and the new `src/util/PluginShelf.h` policy header:

- "На ридере" row/detail state is shown only when ALL shelf gates hold:
  the manifest declares `download.dest_dir` (a config `dest_dir` override can
  move real downloads but never what the shelf plans, opens or deletes), the
  planned name is one safe filename component with a reflowable (`.epub`,
  `.txt`, `.md`) or XTC (`.xtc`, `.xtch`) extension, and BOTH the file and a
  companion marker `<file>.downloaded` exist. The marker is written only by
  the catalog download flow after the file passed verification, so a user's
  own same-named file in `/books/ybx` (or any other folder) is never displayed
  as on-shelf and can never be deleted through this feature. A marker whose
  book was deleted outside the shelf (e.g. in the file browser) is cleared on
  the next shelf scan, so it cannot later vouch for a copied-in file.
- Tap/Confirm on such a row opens the book (silent reboot into the reader, the
  established LCP-loan pattern) instead of downloading again; the open path
  re-checks the gates and refuses files the reader cannot parse.
- DONE screen after a download: Back is checked before any tap (a header back
  tap is also a screen tap and must not arm the reader reboot); the "Открыть"
  label and the tap-to-read hint appear only when the downloaded file's
  extension is reflowable/XTC — for any other extension the screen shows plain
  completion and Confirm returns to the catalog instead of rebooting into a
  failed reader load. `book.downloaded` now fires from `downloadFinished` only
  after the ZIP-magic check passed, so a rejected body is never announced to
  the server outbox.
- Long-press on an on-shelf row: "Скачать заново / Убрать с полки / Отмена".
  Removal is behind a confirmation dialog and deletes only the shelf-gated
  file (manifest `dest_dir` + book extension + firmware marker), its
  rebuildable caches, its recent-books entry, its `.downloaded` marker and the
  plugin sidecar (`{dest}.meta.json`) resolved through the same template the
  writer used. Settings, tokens, credential stores and every other folder stay
  untouched; when the destination folder cannot be created the download fails
  (no SD-root fallback), so planned and written paths cannot diverge.
- YBX home More popup gains a first "На ридере" entry: the download folder is
  resolved lazily on the first More press and cached for the home instance (no
  SD reads or JSON parsing on the home enter path). It opens the FileBrowser
  locked to that folder: short Back, long-press Back and a missing folder all
  stop at the shelf root instead of climbing to `/books` or the SD root.
- Finished `.epub` downloads are checked for ZIP magic (rejected as "Сервер
  прислал не файл книги", file removed). Storage write failures report
  "нет места/ошибка записи"; an empty server-provided download URL and a
  rejected filename now report distinct messages in every catalog.

### Host verification

- `pio run -e default` (X4 profile): passes after the review fixes; RAM
  66,712/327,680 bytes, Flash 5,550,937/6,553,600 bytes.
- Host suite (CMake/CTest under `test/`): 905/905 pass, including the new
  `plugin_shelf` suite (7 cases) driving `src/util/PluginShelf.h` — the exact
  dest_dir/extension/marker rules the shelf and removal paths call. No suite
  drives PluginCatalogActivity or HomeActivity UI; the other checks only prove
  the shared primitives (protected paths, safe removal, filename planning,
  i18n keys) still hold.
- Everything below is unverified until run on hardware.

### Owner acceptance checklist (device)

1. Catalog (YBX) → download a book → progress → "Книга скачана" → OK → reader
   opens that book at page 1; Back from the reader behaves normally.
2. Re-enter the catalog: the row reads "На ридере"; Confirm opens the book
   without network transfer; the footer button label is "Открыть".
3. Long-press the row → "Скачать заново" re-downloads and overwrites without
   extra prompts; → "Убрать с полки" shows the confirm with the book title;
   confirm removes the book, its `*.epub.downloaded` marker, its
   `*.epub.meta.json` sidecar and its caches/recent entry only (other books,
   settings, login intact); the row returns to normal download state.
4. Home → More → "На ридере" (offline, airplane Wi-Fi off): folder of
   downloaded books opens; open/rename/delete work; short Back at the folder
   and long-press Back never climb above `/books/ybx` (Back at the shelf root
   returns home). With the plugin absent or Plugin System disabled, the entry
   is not shown.
5. Copy a personal file into `/books/ybx` whose name matches a catalog row's
   planned name: the row must NOT read "На ридере" (no firmware marker) and
   long-press offers no removal; downloading the real book still writes the
   file plus marker. Then delete a firmware-downloaded book inside Home →
   More → "На ридере" and copy a personal file under the same name: the row
   must again NOT read "На ридере" (the orphan marker was cleared on scan).
6. On the DONE screen tap the header back arrow: return to the catalog
   without rebooting into the reader.
7. Server fault returning non-EPUB body → "Сервер прислал не файл книги", no
   file or marker left in `/books/ybx`, and no `book.downloaded` event in the
   server outbox; a valid download still announces exactly once.
8. Same book from two providers → two files (filenames carry ids) → both rows
   show "На ридере".
9. SD full or unplug mid-download → "Ошибка записи"/failure, no `.part`
   leftovers on the next attempt, retry works; deleting `/books/ybx` and
   filling the root so `mkdir` fails → "Ошибка записи" and nothing written to
   the SD root.
10. WB image-only book opens with cover; all four rotations (portrait,
   inverted, landscape CW/CCW) render rows, popup, DONE screen and shelf
   entries.
11. Repeat 1–4 twice to watch heap-driven behavior (auto-reboot is silent; no
   crash/reboot loops while browsing).
