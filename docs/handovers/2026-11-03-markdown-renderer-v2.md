# YBX Reader P2-I (#56) · Markdown renderer and TOC v2

Parent: draft PR #15 Native Notes, stacked above Native Apps #13.
Source context: independently written adaptation of Markdown reader
ideas reviewed in PaperBit (chippwalters/crosspoint-reader,
commit c26c82e8f52b; MIT). No code copied from PaperBit.

## What actually changed

The pre-existing YBX firmware already opens .md in the EPUB reader,
but TxtToHtml previously escaped the whole Markdown source as plain
text. MarkdownStream.h adds a streaming Markdown subset into that
existing path. It does not create another reader or another library
of book identities.

Supported: ATX h1-h6 headings with optional trailing hashes; ordinary
paragraphs, unordered and ordered lists, simple block quotes,
fenced backtick/tilde code blocks, inline strong/emphasis/code;
escaping unsafe HTML, UTF-8 text, CRLF and BOM handling. Link labels
and targets are rendered as inert, escaped text. Network actions and
raw HTML from Markdown are never embedded or executed.

Not CommonMark-complete: tables, images, nested list indentation,
complex inline nesting, reference links, task-list checkboxes and
raw HTML are not rendered structurally. No automatic remote-image
download or file resource traversal.

The scanner and HTML converter use the same parser and heading
ordinal scheme md-h0001, md-h0002, etc. TOC entries now point to
real HTML IDs, reusing existing EPUB anchor page mapping and
chapter picker. Up to 128 headings are indexed; if a file has any
valid headings, its filename is not duplicated as an extra TOC root.
Heading-less files retain the original filename TOC entry.

Memory policy: a 512-byte stream input block; at most 2048 bytes
buffered per source line, with long lines escaped incrementally.
Malformed UTF-8 sequences are replaced with ? before reaching the
XML renderer. No on-device heap measurement has yet been performed.

## Cache migration and edited Markdown

Plain TXT output/cache version remains unchanged.
Markdown HTML marker changes from MD_CACHE_VERSION: 1 to
MD_CACHE_VERSION: 2.

A markdown-index-v2.fnv sidecar binds full metadata/TOC cache to
exact Markdown file bytes. If sidecar missing/wrong/outdated, even
for equal-length edited Markdown, Epub::load rebuilds metadata/TOC
and HTML/sections together. Ready marker removed before rebuild,
written only after successful metadata build. Original Markdown
source remains unchanged. 64-bit FNV is a change detector, NOT a
cryptographic file identity or authorization check.

Important open limitation: progress.bin stores rendered page and
optional visible-text offset. V1 Markdown was literal text; V2
formatted HTML changes pagination and source offsets. This patch
does not silently rewrite progress.bin or claim accurate translation
of old saved pages. Dedicated progress migration policy and old-cache
device fixtures are required before release. Reader sessions, KoSync,
bookmarks, clippings are not changed by this patch.

## Verification and remaining gates

Host tests: deterministic heading anchors, 1-byte reader chunks,
Unicode/BOM, lists/quotes/code, HTML injection escaping, malformed
UTF-8 and XML controls, long lines, failed I/O, strict XML parse
through Expat, equal-length edit fingerprints. Tests and board build
results are recorded after final validation.

Still open:
- Existing V1 Markdown progress/resume after V2 cache migration,
  same-length edits, old index refresh, TOC jump: physical SD fixtures.
- Actual X3/X4/X4Pro renderer/pagination/font fallbacks and touch/
  button chapter navigation in all supported orientations.
- Conversion latency, max contiguous heap, peak stack, SD reads,
  battery/sleep/wake measured with before/after fixtures. Extra
  full-file TOC/fingerprint scans may increase cold-open time.
- Stable sync identities, note/highlight sync across YBX Server,
  Desktop and PocketBook must be separately designed, authenticated
  and tested; this feature is offline-only.
- QEMU/real-device end-to-end acceptance. No OTA, device flash,
  release or merge to main based solely on build passes.
