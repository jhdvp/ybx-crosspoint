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
  *(Superseded on 1 November: a manifest that chooses the download folder can
  write that marker name itself. See "Shelf provenance" below.)*
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
  *(Superseded on 1 November: the sidecar is taken from the record of what the
  download actually wrote, not from the manifest template. See "Shelf
  provenance and locked root" below.)*
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
   *(1 November: the marker is now a record under `/.crosspoint/shelf/` — see
   the checklist in the next section.)*
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

## Shelf provenance and locked root — 1 November 2026 (second-round review fixes)

Three review findings on the shelf above. No new strings, settings, activities or
allocations; the record folder holds one small file per downloaded book.

- **Removal no longer trusts the manifest sidecar path.** Previously
  "Убрать с полки" deleted `download.sidecar.path` after substitution, gated only
  by `ProtectedPaths::isPluginPath`. A manifest with
  `"download":{"sidecar":{"path":"/.crosspoint/settings.json"}}` and no `body`
  therefore wiped all firmware settings. Removal now deletes a sidecar only when
  `downloadBook` recorded writing it for that exact book, and the recorded value
  must additionally be a `.json` file sitting directly in the manifest's own
  `download.dest_dir` (`pluginshelf::safeSidecarPath`). Anything else — a
  settings file, another plugin's data, a nested folder, a non-JSON name —
  deletes nothing.
- **Provenance moved out of the plugin-writable folder.** The
  `<book>.downloaded` marker next to the book could be planted by any manifest
  that controls `dest_dir`, a sidecar path or a bundle entry, so a user's own
  file could be shown "На ридере" and made removable. The firmware now writes one
  record per book under `/.crosspoint/shelf/<md5 of the book path>`; that prefix
  is in `ProtectedPaths::SENSITIVE_FILES`, so every plugin-supplied write path
  (download destination, sidecar body, bundle file) is refused by the checks that
  already exist. Per-book files rather than one `shelf.json` list: appending to a
  JSON list would need a read-modify-write of the whole file plus a heap buffer
  sized by how many books were ever downloaded. `.downloaded` targets are still
  rejected outright for downloads, planned filenames, sidecars and bundle
  entries, so nothing new lands in the old marker namespace; the old markers are
  simply no longer read. On-shelf, open and remove also require a regular file,
  because `Storage.exists()` is true for a directory named `book.epub`.
- **One canonical spelling for the download folder.** `FsHelpers::canonicalFolderPath`
  (absolute, no trailing slash) is applied to `download.dest_dir` in
  `pluginhttp::pickDownloadDir`, in `pluginshelf::plannedPath`/`safeSidecarPath`
  and to both `basepath` and `lockedRoot` when the home "На ридере" browser
  opens. A manifest writing `"dest_dir": "/books/ybx/"` previously made the
  locked root `/books/ybx/`, which never equalled the `/books/ybx` the browser
  derived, so short Back climbed out of the shelf and the header name went blank.
  The dir-up step now goes through `pluginshelf::parentWithinRoot`, which clamps
  to the locked root even for a path spelled above it.

### Host verification

- `pio run -e default` (X4 profile): SUCCESS; RAM 66,712/327,680 bytes (20.4%),
  Flash 5,552,449/6,553,600 bytes (84.7%) — +1,512 bytes of Flash over the
  previous shelf build, no RAM change.
- `pio run -t unit-tests` (CMake/CTest under `test/`): 912/912 pass, up from 905
  before these fixes. The new cases drive the attack shapes directly —
  `plugin_shelf` (12 cases: record path outside every plugin-writable path,
  sidecar confinement incl. the `/.crosspoint/settings.json` attack,
  marker-suffix rejection, `parentWithinRoot` clamp), `protected_paths` (shelf
  records sensitive, `/.crosspoint/plugins/...` still reachable), `fs_helpers`
  (`canonicalFolderPath`).
- No suite drives PluginCatalogActivity or FileBrowserActivity: the record write
  on download, the read-back at removal, and the browser's locked root are
  exercised only by the firmware build compiling them.
- Everything below is unverified until run on hardware.

### Owner acceptance checklist (device)

1. Download a book → check `/.crosspoint/shelf/` (web file list or the SD in a
   computer): one small record file appeared, containing the sidecar path, or the
   text `none` when the manifest has no sidecar. The book folder itself must gain
   no `.downloaded` file.
2. Re-enter the catalog: the row reads "На ридере"; long-press → "Убрать с
   полки" deletes exactly the book, the recorded `.json` sidecar and the record.
   Settings, Wi-Fi/OPDS/KOReader logins, other books and other plugin folders
   stay intact.
3. Sidecar path attack: install a catalog whose `device.json` has
   `"download":{"sidecar":{"path":"/.crosspoint/settings.json"}}` and no `body`.
   Download a book, then remove it from the shelf: `/.crosspoint/settings.json`
   must still exist and the settings must be unchanged.
4. Forged provenance: with a plugin whose `dest_dir` is `/books/ybx`, copy a
   personal file into that folder under exactly a catalog row's planned name and
   plant a `<name>.epub.downloaded` marker next to it. The row must NOT read
   "На ридере" and long-press must offer no removal.
5. Marker collision: a `device.json` whose `filename_tpl` ends in `.downloaded`,
   whose `sidecar.path` ends in `.downloaded`, or whose `bundle` list has a
   `.downloaded` entry → the download is refused (unsafe filename / bundle abort)
   and nothing is written.
6. Directory masquerade: create a *directory* named `<planned book>.epub` inside
   `/books/ybx`. The row must NOT read "На ридере", Confirm must download
   normally, and no shelf action may hang or fail on the folder.
7. Manifest with `"dest_dir": "/books/ybx/"` (trailing slash): Home → More →
   "На ридере" opens with "ybx" in the header, and short Back at the root returns
   home instead of listing `/books`. A book downloaded into that folder must show
   "На ридере" (one spelling, no second record).
8. Sub-folder inside the shelf (`/books/ybx/Sub`): short Back climbs to
   `/books/ybx` and stops there; long-press Back and a deleted folder land on
   `/books/ybx`, never above.
9. Books downloaded by the previous build keep their `<book>.epub.downloaded`
   marker and gain no record, so their row reads as not on the shelf; re-download
   one and it reads "На ридере" again.
10. Repeat 1–3 twice for heap-driven behavior (no crash or reboot loop while
    browsing the catalog, the popup and the shelf folder).
