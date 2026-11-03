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
the existing cache for book.bin and the absence of the successful
markdown-index-v2.fnv marker. If legacy progress.bin exists, copy its
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
