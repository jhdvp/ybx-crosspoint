# Native achievements

Statistics opens Achievements without another UI host or a background task.
Received and In progress are real tabs; a YBX Reader tab is shown only if the
account API actually returns reader awards. Four bounded text cards form the
current page. Portrait uses two columns; landscape uses four, with text bands
adapted to available height. Open a card for its description and date details.
Back goes to the list, then Statistics, then Home. Physical and touch navigation
share the existing profile screen's interaction and Wi-Fi lifecycle.

## Data and limits

SD plugin 1.9.0 adds `ybx.achievements`, the HTTPS GET request for
`/api/v1/crosspoint/achievements?limit=50`. The existing token placeholder and
scope are reused. Provider credentials and sign-in stay on YBX. This API currently
returns Yandex received/pending awards and an empty reader-awards array. Native
code never manufactures local awards. The numeric progress field is optional;
currently the service supplies progress/conditions as description text, so no
percentage is guessed from that text. The first observation time is labeled
separately from the earned time. Missing dates/progress remain unknown.

The response streams to SD with a 128KiB limit, then is validated by the existing
SDK JSON SAX parser and a bounded native model. Four cards are retained per page buffer (shown and working);
page changes rescan the saved file without network requests. Each service list
is limited to 50 cards; has_more is shown with a plus/count and a partial-list
notice. This API has no cursor: the reader does not claim to page beyond the
returned limit. Award images are not fetched by this text-card slice.
Titles/descriptions have fixed UTF-8 bounds and an ellipsis on truncation.

`achievements.json` in the selected YBX plugin folder has a 65-byte prefix:
64 lowercase SHA-256 token-binding characters and newline, followed by the raw
account JSON. No raw token is stored. Relinking/token rotation invalidates the
old snapshot. A separate response file is parsed before replacement; the writer
checks the exact copied size and file close, and uses the existing recoverable
pending/backup transaction. The queue helper now also accepts a streaming writer;
its original string-writing wrapper and event delivery behavior are preserved.

An update that cannot be saved retains the entire previous list, not a mixture
of new first-page data and old later pages. Auth/network/validation failures also
retain the last token-bound offline list. Reconnect-required status does not
claim live authorization. The backend checked_at timestamp, when available, is
shown as a check time in UTC, not as an earned date or a fabricated fetch time.

## Resource ownership and verification

The profile allocates two page buffers only while the awards view/detail is open,
then releases them on returning to Statistics. The SAX parser is checked-allocated
for a cold SD scan and released before returning. The 128-byte read/copy buffers
stay under the small-stack policy; page resets write fields in place rather
than constructing an entire page on the MCU stack. No full response, whole
history, PNG or framebuffer is retained in RAM. Refresh is synchronous with the
existing cooperative 12-second limit; inherited DNS/TCP can exceed it, and controls
are disabled while waiting. Only a Wi-Fi connection started by this screen is
closed on exit.

Host checks cover streamed chunks, page boundaries, unknown/zero/invalid progress,
UTF-8 clipping, invalid/truncated/trailing JSON, maximum counts, cache binding,
keyboard reachability and geometry in all rotations. The existing transaction
failure tests cover the preserved queue wrapper. Physical SD durability, runtime
heap/largest-block recovery, latency and touch/input handoffs require device
checks; no hardware result is inferred from builds.
