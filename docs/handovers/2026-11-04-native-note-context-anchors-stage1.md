# YBX Reader P2-I / Kaneo #56 — Native Notes quote context anchors, stage 1

Stacked on draft PR #20 (unique in-memory quote relocation). This is a
firmware-local compatibility stage, not a new server or wire protocol.

## Behaviour

Native Note selection captures up to 16 Unicode codepoints (64 UTF-8
bytes) before and after the highlighted quote. Source offsets refer to
MarkdownStream -> XHTML visible characterData, not raw Markdown bytes.
Capture requires an exact match of the selected quote at its stored
sourceStart/sourceEnd, a valid and unchanged source note and sufficient
runtime heap. If any condition fails, the main clipping is still saved
normally, without context. The original quote is associated with a
stable 32-hex clipping ID and FNV64 of the exact saved quote text.

On reopening an edited note, quoted text + prefix + suffix must all
match, at whole-token boundaries, at ONE unique location in the current
visible XHTML text. Duplicate quotes with different surrounding text can
be disambiguated. Identical quote and context at multiple locations
remains ambiguous; changed context prevents relocation. Legacy clips
without context continue to use the prior unique-quote strategy.
Corrupt, unreadable, partial or quote-mismatched stored context NEVER
falls back to uncontextualized matching.

Accepted matches are ephemeral reader spans only, for consistent
highlight and tap hit-testing. Saved clipping IDs, original offsets,
layout signature, pending upload, deletion tombstones, revision and
server data are NEVER rewritten.

## Storage and recovery

An optional companion file is located next to the ClippingStore V4
binary: /.crosspoint/clippings/epub_<path-hash>.bin.anchors
Its magic is YBXCTX01, with up to 64 fixed records containing
32 lowercase-hex ID bytes + NUL, bounded UTF-8 prefix/suffix,
the FNV64 original-quote fingerprint and an FNV32 integrity checksum.
All multibyte fields have explicit endian-independent encoding.

Writes stream old records into a temporary file, flush/close, reopen
and validate all new bytes, then rename existing canonical to backup,
rename temp to canonical and roll back if the final rename fails.
Reads check the entire file and can recover a valid .anchors.bak when
the canonical file is missing. No unbounded in-memory record list.
The file/backup/temp follow the book through ClippingStore moveBook
and are removed by deleteForFilePath. Per-clipping deletion leaves a
bounded unused record: IDs do not repeat. After 64 anchors, normal
clipping capture continues but optional anchor creation stops.

An FNV checksum detects accidental damage, not adversarial tampering.
FAT metadata remains non-atomic under sudden power loss. Real SD tests
remain mandatory. Context capture requires >=32 KiB free heap and
>=12 KiB contiguous block, a valid Native Note within its maximum
file size, and an 8..512-byte selected quote.

## Compatibility and limits

Only generated Native Note filenames are eligible. ClippingStore V4,
YBX Server sync_version=2, ordinary EPUB/Markdown, KoSync page progress,
server clipping IDs/tombstones and existing clients are unchanged.

The companion file is local: copying just a .md file does not copy its
clipping store or context. Cross-device context synchronization,
account-scoped canonical book alias/migration, portable export of
anchors and version negotiation require a separate server/client
contract. Older or 64-hex remote clip IDs without local anchors
retain prior exact-unique fallback. If existing context data are
damaged, those matching attempts fail closed.

The content-aware match remains a heuristic: identical quotation and
adjacent context can be moved together elsewhere. Stronger structural
anchors or explicit user confirmation remain desirable.

## Remaining release gates

- Real X3/X4/X4 Pro tests: quote capture in Unicode, lists, code,
  bold, multiline; actual rendered offsets, taps, low-memory and SD.
- Genuine staging KoSync on two devices, divergent revisions, offline
  deletion/retry and unchanged account boundaries.
- SD interrupted rename/short write and power loss on actual FAT.
- Heap, cold-open latency and E-Ink UX.
- Backward-compatible optional server context fields and audited
  migration of old binary-key clipping collections, with no ambiguity.

No main merge, physical flash, OTA, production deploy or release.
Kaneo #56 remains in progress. Final verified figures added after QA.

## Final verified build and regression evidence · 2026-11-04

- Full host CMake/CTest: **1237 / 1237 PASS** (12.88 seconds).
- NativeNoteContextStore: **14 / 14 PASS**, including FNV64 quote
  fingerprint mismatch, old/anonymous ID compatibility, checked
  append, repeated writes, 64-record cap and simulated SD failures.
- NativeNoteReanchor: **14 / 14 PASS** including real TxtToHtml/Expat
  visible characterData parity, Unicode, repeated phrases and exact
  prefix/suffix matching.
- ClippingStore C3/S3 host: **26 / 26 PASS**, with book moves,
  deletion and rollback preserving/removing .anchors companions.
- YBX Site existing server clipping-contract pytest:
  **19 / 19 PASS**. Server files, database and API were not changed.
- ESP32-C3 default X3/X4 PlatformIO: **PASS**;
  flash 5,726,193 / 6,553,600 bytes (87.4%);
  static RAM 66,904 / 327,680 bytes (20.4%);
  firmware.bin 5,740,720 bytes;
  SHA256 6a34e29e258594f5ab163d7d5d66434499588f73108279ab7383037a65c7db78.
- ESP32-S3 x4pro X4 Pro PlatformIO: **PASS**;
  flash 5,565,022 / 6,553,600 bytes (84.9%);
  static RAM 111,956 / 327,680 bytes (34.2%);
  firmware.bin 5,570,288 bytes;
  SHA256 a0398734eca9fd045fc05f0ad8fc061343ad9bbd00fd028558cef51cfa83e59e.
- Flash growth vs parent PR #20:
  +5,450 bytes C3 and +5,712 bytes S3. Static RAM unchanged.
  These are LINKED static figures, not measured live heap or power.
- Full artifacts and raw logs:
  ~/Projects/Ybx/artifacts/ybx-note-context-anchors-20261104.
  Local freeink-sdk patches generated by PlatformIO were not staged.
- No main merge, OTA, real device flashing, production deployment
  or live server clipping mutation. SD fault injection uses mock
  storage; hardware acceptance remains mandatory.
