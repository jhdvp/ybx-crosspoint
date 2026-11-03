# YBX Reader · P2-I Markdown V1 -> V2 saved-position migration

Kaneo #56. Changes stacked above draft PR #16 (formatted Markdown
renderer). No main merge, OTA, production or physical device flash.

## Why the old progress format is incompatible

Markdown V1 was literal plain text. V2 turns its Markdown into
formatted paragraphs, headings, lists and code blocks. A V1 page number
or visible-text offset is NOT a stable semantic reading position in V2;
different markup, fonts and pagination mean that blindly restoring old
byte offsets/page indices can jump to unrelated text.

The existing reader stores 4, 6 or 10 bytes of progress.bin: spine
index, page number, optional total pages and optional text offset.
This migration deliberately preserves that exact original binary.

## Protection policy before reindexing

Before Epub::load can rebuild the Markdown TOC/HTML/sections, inspect
the absence of the successful markdown-index-v2.fnv marker.
Even if book.bin is absent after a corrupt/interrupted rebuild,
a remaining legacy progress.bin must still be protected. Copy its
EXACT 4/6/10 bytes to:

    /.crosspoint/epub_<book-cache-id>/progress.md-v1.bin

Implementation writes progress.md-v1.bin.tmp, flushes and closes,
reopens and compares exact byte content, and then renames to the
backup location. An existing identical backup is reused (idempotent);
a conflicting/corrupt backup is never overwritten.

A failure at any point stops opening/reindexing that Markdown file.
The canonical progress.bin remains unchanged and no V2 paginated
progress is written by that failed open attempt. User sees an error
rather than a silent loss of the only saved position.

This is not guaranteed metadata-atomic on FAT: failures during
directory/SD operations may still require PC filesystem repair.

## Resuming safely

- If the old file has 6 or 10 bytes with a meaningful page and page
  count, AND the Markdown source is at most 64 KiB, use the
  existing Go-to-% renderer to calculate the nearest position from
  the previous page fraction. No old V1 text offset is used.
  This fully repaginates this bounded-size document, so it is an
  APPROXIMATION. A localized popup tells the user.
- If the old format has only 4 bytes, a nonsensical page/total,
  page zero, or the source exceeds 256 KiB, open page one. Avoid
  forcing a huge full-book build solely for migration. Explain with
  a localized popup that the old raw position has been preserved.
- After successful display, the normal reader writes fresh V2
  progress.bin; progress.md-v1.bin stays as historical recovery
  material and is not deleted automatically.
- The legacy backup is not a sync identity, user-visible bookmark
  or universally portable KoSync cursor. It is an exact byte snapshot
  from an old page layout and can be manually recovered alongside the
  matching V1 firmware/cache if required.

Only Markdown files trigger this policy. Existing EPUB/TXT progress,
bookmarks, KOSync and user clipping files are not explicitly migrated
or changed. A V2 index marker means the reader follows its ordinary
progress loading path. The FNV sidecar is a layout/source cache
detector, NOT an authentication or cryptographic content hash.

## Validation

Host test plan verifies the 4, 6 and 10-byte progress layouts,
corresponding estimated fractional resume and fallback, bounded
full-pagination selection, exact snapshot preservation, no-write
for new files or V2-marked books, retry/idempotency, conflicting
snapshots and SD offline/short-write/close/read/rename failures.

Remaining acceptance gates: physical SD with interrupted copy and
rebuild, an old V1 cached Markdown fixture, actual X3/X4 and X4 Pro
page/text visual comparison, multiple page settings/orientations,
successful and failed KoSync, long Markdown cold-open time, exact
screen layout and popup visibility, live max-contiguous heap and
battery. The requested exact semantic migration of highlight/quote
ranges and cross-client note identity remains separate work.

## Final verified QA evidence (2026-11-03)

- Complete CMake/CTest regression: 1191 / 1191 PASS, 13.89 seconds.
  Includes 19 new MarkdownLegacyProgress tests (4/6/10B layouts,
  missing book.bin, V2 marker/no progress, multiple failure modes,
  repeat attempt, nonzero invalid spine and bounded approximation).
- ESP32-C3 default X3/X4 PlatformIO: PASS.
  Flash 5,714,359 / 6,553,600 bytes (87.2%);
  static RAM 66,904 / 327,680 bytes (20.4%).
  firmware.bin 5,728,880 bytes.
  SHA-256 ffe81852d43ce538cb4b360b95d611393e0f084d2f8edffa43f622e556458b19.
- ESP32-S3 X4 Pro PlatformIO: PASS.
  Flash 5,553,358 / 6,553,600 bytes (84.7%);
  static RAM 111,956 / 327,680 bytes (34.2%).
  firmware.bin 5,558,624 bytes.
  SHA-256 2bd10e579b7342238d7d5bf7de760a52e4f8669410444f25b4c35ff5a3a43c4f.
- Flash delta compared with parent PR #16:
  +1,942 bytes C3 and +1,864 bytes S3. Static RAM unchanged.
  No actual free-heap, power or SD throughput claim from these metrics.
- Local raw build logs, both firmware images and checksums:
  ~/Projects/Ybx/artifacts/ybx-md-progress-migration-20261103.
  This folder is for validation, not OTA or release publication.
- Simulated HalStorage failures do not substitute for FAT
  metadata-atomicity, unexpected electrical power loss, real EPD/input
  testing or cross-client KoSync position acceptance.
- There was no physical device flashing, OTA, production deployment,
  merge or release.
