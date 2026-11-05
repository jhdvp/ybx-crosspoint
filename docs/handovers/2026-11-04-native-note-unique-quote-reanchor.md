# YBX Reader P2-I #56 — unique quote reanchor for edited Native Notes

Stacked on draft PR #19 (stable Native Notes clipping collection identity
and source-revision guard). No new server API, user-facing switch or
mutation of the existing ClippingStore V4 binary format.

## Semantics and conservative scope

An edited Native Note keeps the same opaque note-<16hex>.md filename and
the same server clipping collection. PR #19 hid old highlights after
the source changed, because old numeric offsets may then address entirely
different words. This stage restores *some* highlights without modifying
their saved positions.

The production TXT/Markdown conversion emits XHTML from MarkdownStream.
ChapterHtmlSlimParser's source offsets count **visible UTF-8 Unicode
codepoints in XHTML body characterData**, including inter-tag newlines;
they are NOT offsets into the original .md bytes. Therefore we reuse
the exact MarkdownStream rendering emitter and consume its generated
XHTML body text nodes via a small bounded tag/entity parser, including
the exact initial/final body newline used by TxtToHtml. This avoids
guessing offsets by searching raw Markdown bytes (which is wrong after
headings, emphasis, XML escapes and Unicode).

The helper never holds the generated XHTML markup in memory. It
collects only its visible text, bounded at 2 * NativeNotes max file
size + 64 bytes, and runs only for fully valid, generated Native
Notes (title and body within <=4096 B). Renderer XML-escaping supports
&amp;, &lt;, &gt; and refuses unknown entities.

After loading ClippingStore, at most 16 stale *candidate* clippings with
known, changed nonzero layout signatures and quoted text 8..512 bytes
are examined. Only an **exact UTF-8 quotation** occurring **once**
across the entire current visible-text stream and at whole-token
boundaries yields a temporary source-codepoint span. Multiple equal
occurrences, missing text, format or Unicode mismatch, very short or
long quotations and any memory/IO failure produce no relocated span.
Full scanning only runs when >=32 KiB free heap and >=12 KiB largest
contiguous allocation are available; otherwise we keep old highlights
hidden and the normal reader continues.

The ephemeral spans are stored in a fixed-size array in the current
reader activity; both screen-highlight painting and tapping a clipping
use the same accepted relocated span. Rendering checks at most 16
relocated spans once per word rather than doing 16 checks for each
clipping. Native notes without a relocated span retain PR #19's
revision guard. EPUB and arbitrary Markdown are untouched.

**This is a conservative textual HEURISTIC, not proof that identical
quotation text denotes the same semantic passage.** It deliberately
does not do fuzzy whitespace normalization, infer context for repeated
passages, or select the closest occurrence by stale numeric offset.
A passage deleted then reintroduced elsewhere could be reattached
because it is unique; confirming that a quoted passage has the same
meaning requires a future prefix/suffix-aware anchor and/or explicit
user approval.

## Persistence and sync invariants

- Original clipping ID, original source start/end, layout_signature,
  timestamp, syncRevision, pendingUpload and deletion journal untouched.
- No write to the ClippingStore file for reattachment. No server
  PUT/GET of modified positions, no KoSync progress changes, no extra
  collection alias and no tombstone generation.
- The transient span is recomputed on each note open and vanishes on
  reader exit. This avoids automatic irreversible migration and
  cross-device conflict or revision inflation.
- Existing filename-keyed sync remains account authenticated by the
  existing verified HTTPS transport from PR #18.

## Verification and remaining gates

Host tests compare streamed visible text with **real TxtToHtml output
parsed by Expat** (Unicode, formatting, quotes, lists, fences,
escapes, external link labels). They also check unique matches,
repeated phrases, word-boundary false positives, codepoint offsets,
title-edit displacement, malformed/oversized files and quote bounds.
The production selection/draw/tap-target extraction harness tests an
ephemeral relocated span, not just the pure helper.

Remaining before release:
- Actual X3/X4/X4 Pro scroll, tap, layout and source range checks
  with two devices and independently edited source; verify no
  incorrect highlight when rendered text changes around formatting.
- Hardware cold-open latency, contiguous heap and battery cost with
  16 long clippings, intermittent SD and low-memory reader sessions.
- Power loss during note creation/edit and clipping sync.
- Stable cross-client canonical book ID beyond Native Notes,
  permissioned alias migration of old binary-key collections,
  stronger cryptographic/structural anchors with prefix and suffix
  context and explicit resolution of ambiguous duplicates.
- No OTA, main merge, production deployment, firmware flashing or
  physical-device validation based only on host and PlatformIO.

Final test counts, firmware sizes and SHA-256 values are recorded after
full QA and linked in the stacked draft PR.

## Final verified builds and regression (2026-11-04)

- **1218/1218 CMake/CTest full host PASS**, 19.91 seconds,
  including the C++ production draw/tap-target extraction harness.
- **9/9 new NativeNoteReanchor GTests PASS**, verifying exact
  stream-to-XHTML characterData equality through Expat, Unicode
  codepoint offsets, uniqueness, repeats, boundaries and failure.
- Existing **YBX Site clipping protocol 19/19 pytest PASS**;
  server schema/API/source unchanged.
- **ESP32-C3 default X3/X4 PlatformIO PASS**:
  flash **5,720,743 / 6,553,600 bytes (87.3%)**;
  static RAM **66,904 / 327,680 bytes (20.4%)**;
  firmware.bin **5,735,264 bytes**;
  SHA256 **4aded7f2b6b52f79082f6059aa84c2798392ba614331b57f69e3354068393743**.
- **ESP32-S3 x4pro X4 Pro PlatformIO PASS**:
  flash **5,559,310 / 6,553,600 bytes (84.8%)**;
  static RAM **111,956 / 327,680 bytes (34.2%)**;
  firmware.bin **5,564,576 bytes**;
  SHA256 **a7837dd833941536e73c5de6efeb79c35b4498eddeb8f993498e0272435ebed6**.
- Relative to parent PR #19 flash grew **+5,306 B C3** and
  **+4,912 B S3**; static RAM unchanged. Real heap/energy/SD
  cold-open latency were NOT measured. The reader's per-instance
  fixed in-memory relocation array uses around 192 bytes.
- Full logs, firmware.bin images and sha256 files remain at
  ~/Projects/Ybx/artifacts/ybx-note-unique-reanchor-20261104.
  No flash to hardware, main merge, OTA, release or production deploy.
- The freeink-sdk submodule appears modified by PlatformIO's
  local patch step; those changes were deliberately not staged.
