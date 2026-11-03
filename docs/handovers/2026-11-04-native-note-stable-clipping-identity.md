# YBX Reader P2-I #56 — stable Native Notes clipping keys and revision-guarded highlights

Parent: draft PR #18 (persist clip IDs at capture and use verified HTTPS).
This stage modifies the firmware only; YBX Server's established
/api/v1/clippings/{document} sync_version=2 protocol is unchanged.

## Why this is separate from reading progress

Native Notes are small Markdown files with an immutable random
note-<16 lower hex>.md basename. Editing H1 or body changes the binary
KoSync hash. With default content-hash matching, that would create a
new clipping collection on the server after each edit.

For clipping sync of managed notes ONLY, use the existing filename MD5
in place of the primary document hash. This stays consistent across
edited contents, different directories, /Read moves and SD copies,
provided the generated opaque filename is preserved. The client uses
existing verified-HTTPS clipping sync and stable random clipping IDs.

Do not override the normal progress hash: KoSync progress and smart
sync still use the configured match method and alternate probe.
Two different note revisions have different pagination and an
identical remote progress could be unsafe. ReaderSessionJournal
also remains unchanged.

Only exact native-note-generated filenames are recognized; paths
must be absolute with no empty, dot or dot-dot segments. The filename
is not a secret or proof of document equality, only a per-account
lookup key. Deliberate duplicate filenames can still cause conflict.

## Avoid painting offsets from an edited note

For generated native notes only, the capture-time layout_signature
field now carries a source-revision fingerprint (bounded FNV-1a 32-bit,
domain-separated as ybx-native-note-md-v2:). The note is read once on
load, within its maximum native-note format budget, and the signature
is reused for selection and drawing. A note highlight paints only
when the saved signature matches, source offsets are known and overlap,
and spine matches. The same guard applies to clippingAtPoint hit-testing
so stale quotes cannot be opened by tapping unrelated words. The old page/word fallback is forbidden for notes:
it cannot prove correctness after an edit.

If the source is missing/oversized/unreadable, the note can still
be read but quotes are NOT painted. The quotes themselves remain
listed, exportable and syncable; their IDs and text are preserved.

Ordinary EPUB and third-party Markdown keep the existing render-spec
signature and highlight handling. Local clipping file VERSION=4 and
server sync_version=2 remain unchanged.

The 32-bit FNV fingerprint is NOT cryptographic or collision-proof.
It is a cheap conservative detector; identical bits do not prove two
files have identical content. A future Markdown parser revision must
change this signature domain before reusing its source offsets.

## Compatibility and remaining work

This does NOT automatically migrate previous experimental clip
collections stored under content hashes, and deliberately does not
alias unlike server documents. Renaming the generated note basename
to another name loses this special identity. Copying the same note
name to two devices that later edit it independently shares the quote
list, but only quotes from matching content revisions are highlighted.

This is NOT a portable cross-edition quote anchor, PocketBook stock
notes protocol v2, arbitrary EPUB alias migration or text-context
relinking. Relocation after an edit requires unambiguous context-based
text matching. False placement is deliberately avoided.

## Tests / release gates

- Filename selection: invalid names, traversal, directory moves,
  stable clipping key after same-size edits and unchanged EPUB sync.
- Preview tests: unknown/stale note signature hides a highlight
  while EPUB and same-version native-note clipping remain visible.
- Production drawing and hit-test harness verifies both cases from
  actual EpubReaderActivity::drawClippingHighlights and clippingAtPoint.
- Final full CMake/CTest and both board build evidence appended after
  verification.

Remaining: real X3/X4/X4Pro devices, staging HTTPS KoSync sync,
offline deletion and restart, two divergent note revisions, SD
power cuts, runtime memory, exact quote reattachment and server
account-specific canonical document mapping. No merge or OTA.

## Final verified QA · 2026-11-04

- Full host CMake/CTest: **1209/1209 PASS** (25.69 s), including
  Python/C++ extracted production selection/redraw/hit-target harness.
- Targeted note identity + clipping preview: **25/25 PASS**;
  generated filename validation, directory moves, changed same-size
  Markdown content, stale/matching source signatures and EPUB parity.
- Existing YBX Site clipping wire-contract pytest: **19/19 PASS**.
  Server files, schema and production data were not modified.
- ESP32-C3 default / X3-X4 final PlatformIO: **PASS**;
  flash 5,715,437 / 6,553,600 bytes (87.2%);
  static RAM 66,904 / 327,680 bytes (20.4%);
  firmware.bin 5,729,968 bytes;
  SHA-256 999471ba3de455dbc326de101a5fbf25f46ea68e1f3617e9913152848fea1618.
- ESP32-S3 x4pro / X4 Pro final PlatformIO: **PASS**;
  flash 5,554,398 / 6,553,600 bytes (84.8%);
  static RAM 111,956 / 327,680 bytes (34.2%);
  firmware.bin 5,559,664 bytes;
  SHA-256 e89165b07cfdc6f9a899b34ac1b6d5ba0d4273d0b71f536b0265424f70606aae.
- Flash delta against draft parent PR #18:
  +728 bytes C3, +776 bytes S3. Static RAM unchanged.
  Link metrics are NOT measurements of real runtime heap or battery.
- Raw logs, retained firmware images, sha256 files:
  ~/Projects/Ybx/artifacts/ybx-native-note-doc-id-20261104.
- Both environments were rebuilt incrementally at the final source
  head after the last tap hit-test fix. freeink-sdk was dirtied by
  the PlatformIO patch step only, not staged or committed.
- No real hardware, staging KoSync upload, SD fault injection,
  main merge, OTA, firmware release or production deployment.
