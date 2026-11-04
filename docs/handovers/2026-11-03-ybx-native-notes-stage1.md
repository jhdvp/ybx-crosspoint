# YBX Reader · P2-I Markdown notes, staged implementation

Kaneo #56. Draft stacked on YBX Native Apps PR #13.
Source references studied: chippwalters/crosspoint-reader (PaperBit)
commit c26c82e8f52b, src/activities/notes/NotesListActivity.cpp,
NoteEditorActivity.cpp and lib/Epub/Epub/markdown/MarkdownSection.cpp
(MIT, check bundled notices if copying). Implementation here is new,
independent code using YBX's already existing UI/keyboard/HAL.

## Current actual Markdown support

YBX firmware already identifies .md as a reflowable book (FsHelpers) and
passes Markdown through Txt::streamTxtToHtml. However TxtToHtml currently
escapes content into literal text and HTML line breaks; Markdown headings,
bold/list syntax, links and fenced code are NOT interpreted as formatted
Markdown. Therefore this stage does not claim full Markdown layout or TOC
parity with PaperBit. A separate P2-I implementation with cache-version
migration and cross-device position fixtures must address true rendering.

## User-visible stage 1

YBX Home > More > Apps > Notes. Native note module is compiled for X3/X4
and X4 Pro as a second app beside Memory status. The Native Apps registry
still does not execute SD packages or accept untrusted app binaries.

Users can create an offline note, edit its title or body through the
existing FreeInkUI keyboard, open it with the existing YBX .md reader,
and delete it after confirmation. The file remains a standard portable
Markdown document at:

    /apps/notes/note-<16 random hexadecimal digits>.md

The first line is an H1 (# title); a blank line separates the body.
The random file ID is independent of the title, avoids collisions after
renaming, and stays fixed across edits and USB/export operations.
Files are not uploaded and there is no YBX server notes API assumption.

Current caps: 32 listed notes, 96 UTF-8 bytes/title, 4096 UTF-8 bytes
for the body. Invalid UTF-8, NUL/control characters and malformed note
documents are refused; an unreadable record stays visible as an error
rather than being overwritten silently. The limits apply to UTF-8 bytes,
not to displayed glyphs. The list ignores other/unrecognized files.

For new notes Cancel before both prompts are completed creates nothing.
For existing notes Save only updates the in-memory view after a successful
persist. Failed SD writes produce a visible error, leaving the original
committed note unchanged where the filesystem permits.

## Recovery protocol

The original note is never truncate-written. A new revision is written to
the .md.tmp file, flushed, closed and read/decoded/compared before commit.
On updates, a previously valid .md becomes .md.bak, then the verified
.tmp is renamed to .md. A failed final rename attempts restoration
of the backup. On the next list/load, a valid .bak restores
a missing canonical .md. A corrupt final .md is NOT destroyed by an
automatic migration or overwrite, and an earlier valid backup must not
be deleted when final validation fails. Partial/orphaned .tmp is
never promoted as if it were a committed note.

Deleting a note removes old backup and temp before canonical .md, to
avoid accidental resurrection after a completed delete.

This is application-level recovery, not metadata-atomic FAT journaling:
a power failure during FAT directory modifications may still result
in media errors or orphaned files. Physical fault injection, fully
exhausted SD and card-removal testing remain OPEN. Do not claim
fully durable atomic commits or that host format tests cover storage.

## Preservation and acceptance

Reader progress, KoSync, PluginHub, server source/catalog hierarchy and
session journals are unchanged. The files can be transferred via the
existing USB/file-transfer capabilities, not via a new network service.
No BLE keyboard manager, Wi-Fi, background sync job, or server endpoint
is enabled by this feature.

Host tests cover serialization, Unicode, invalid UTF-8, bounded content,
opaque filenames, path traversal and exact round-trips. Full CMake
regression and PlatformIO C3/S3 build results are recorded on the final
verified commit separately.

Release gate: real-device X3/X4/X4 Pro create/edit/open/delete,
many repeated editor screens, display orientation, truncated long lines,
low heap, no SD/full SD, reboot after interrupted rename, USB exported
file round-trip, power cycle and FAT repair. Cross-client YBX notes sync
and canonical highlight IDs, full Markdown parser/TOC and stable
pagination/resume mapping are separately unimplemented requirements
within #56. Never do OTA/flash/merge based only on compilation.

## Final verified QA evidence (2026-11-03)

- Complete CMake/CTest suite: **1157/1157 PASS** (14.31 sec).
  Includes 8 pure Markdown-document tests and 14 storage tests using
  fault-injectable simulated HalStorage.
- Simulated storage faults verified: short temp write, failed close,
  verification-read failure, failed canonical-to-backup rename, failed
  temp-to-canonical rename + fallback, missing canonical recovery from valid
  backup, orphaned tmp never promoted, corrupt final retains backup, delete
  removes resurrection candidates, failed deletion retains canonical,
  offline SD, and 250-entry bounded directory scan.
- ESP32-C3 default (X3/X4) PlatformIO PASS at the corrected source head:
  flash 5,704,839 / 6,553,600 bytes (87.0%);
  static RAM 66,904 / 327,680 bytes (20.4%);
  firmware.bin 5,719,360 bytes;
  SHA-256 2eab8a0b84faedc64e366caffba01556a545d64074e8ef081e92c9b62788cfc6.
- ESP32-S3 x4pro PlatformIO PASS:
  flash 5,544,382 / 6,553,600 bytes (84.6%);
  static RAM 111,956 / 327,680 bytes (34.2%);
  firmware.bin 5,549,648 bytes;
  SHA-256 3ef50549380bc8f5c39f976ff076f4cebe568442a77ca7e23e026a96375149b6.
- Relative to parent PR #13, flash delta: +17,802 bytes on C3,
  +15,152 bytes on S3; unchanged static RAM. These are compile-time
  measurements, NOT measured runtime peak heap, stack or SD reliability.
- Local retained builds and logs:
  ~/Projects/Ybx/artifacts/ybx-notes-20261103.
- Host fault injection validates NotesStorage control flow with a fake HAL.
  It cannot prove that FAT metadata and physical SD writes survive a real
  interrupted write/power failure. Hardware and QEMU end-to-end remain OPEN.
- No production merge, OTA, firmware release or physical flash.
