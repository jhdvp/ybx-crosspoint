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

## YBX aggregated library tree — 28 October 2026

The merged library (`/library`: Все, Читаю, Хочу прочитать, Прочитано, Серии,
Полки, Загруженные) and the per-source folders are OPDS 1.x Atom behind HTTP
Basic device auth. Those routes reject a `cp_` plugin token, and the JSON
plugin API has no tree, so the firmware browses the tree with the existing
native OPDS browser (`OpdsBookBrowserActivity` and the pinned SDK
`OpdsClient`/`OpdsFeedParser`) instead of a parallel JSON UI. Search,
OpenSearch, paging and signed `?e=&s=` downloads stay on that code path; no
bearer token is put into a URL, and the download log prints the URL without its
query so a capability signature never reaches the serial dump.

- One feed per level: root → «Все»/source → section → shelf/series → books.
  The server pages at `opds_page_size` and the SDK keeps at most 62 entries per
  feed; the change adds no heap-resident list.
- Back returns to the feed level it came from with the row the reader was on
  selected (arrow scrolling counts, not only an activated row). Leaving a book
  card and the reload after a download also return to that card.
- An entry with no acquisition URL (an OPDS 2.0 self-only row) opens a card
  marked "Not available": the primary button is not drawn and neither Confirm
  nor a tap there starts anything. In a flat plugin catalog the same applies to a
  row whose mapped `url` field is empty (the YBX update notice) once the
  manifest declares `download.require_url`; every other catalog keeps its fetch
  button. See [plugin manifest schema](sd-plugins.md).
- `STR_OPDS_UNAVAILABLE` gained a Russian text; other languages still fall back
  to English as the generator documents.

Verification: `test/opds_library_tree` compiles the pinned SDK Atom parser for
the host and pins the server-shaped feeds (root navigation with «Все» before
source folders, the Все folders, a shelf with a notice entry and signed book
cards, a series list with counts and paging, ordered series books, search
groups and facets, the 62-entry budget). `test_item_downloadability.py` compiles
`PluginCatalogActivity::itemDownloadable()` and pins which manifest shapes lose
the download action. Host suite: **907/907 passed**, including the eight parser
cases and the downloadability checks. `pio run -e default`: passed; static RAM
66,712/327,680 bytes, flash 5,545,589/6,553,600 bytes. Neither proves device
behavior.

Not verified: hardware. YBX feeds omit `opensearch:itemsPerPage` and
`startIndex`, so the "page N/M" indicator stays unknown while arrow paging
works. The pinned parser drops an entry whose only link is a `text/html`
alternate; YBX does not emit that shape in these feeds. A book the Yandex
source cannot deliver is still downloaded as an explanatory error EPUB (server
contract, not detectable by the client before the transfer). The notice-row
marking also depends on the manifest: the SD copy in this repo declares
`download.require_url`, and the plugin build served by the YBX site needs the
same field before an installed-from-hub catalog marks the notice.

Owner acceptance on the device (Wi-Fi configured):

1. Settings → OPDS Servers → YBX server
   (`https://my.ybx.app/opds/<device-id>`, Basic username = device id, password
   = device key; with no plugins installed the same browser is Home →
   Catalogs) → Browse.
2. The root lists «Все» first, then the connected source folders. Open «Все»:
   Читаю, Хочу прочитать, Прочитано, Серии, Полки (Yandex linked), Загруженные.
3. Серии → a series: books in series order; the number reads in the card.
4. Open a book card → Back: the same row is selected. Download → after the
   reload the same row is selected and the EPUB appears in the library.
5. Scroll with the arrows to a row that is not the first and open nothing.
   Search from the header: results keep the quoted-query header; Back returns to
   the previous feed with that scrolled row selected, not the top of the list.
   A Next/Previous page turn returns to the same row of the page it left.
6. Next/Previous move through a section; an unknown N/M count is expected.
7. The YBX plugin's update-notice row shows "Not available" and its card has no
   fetch button; Confirm on that card starts no transfer and Back returns to the
   list. Other plugin catalogs (and any row with a file URL) keep their button.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.
