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
