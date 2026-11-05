# Native YBX home and statistics

The Home and Statistics screens use the same account summary as the YBX
clients. They do not run a second reading timer or import the vCodex export.
This slice contains the account summary and weekly chart.
[Achievements](ybx-native-awards.md) extends the same screen host; calendar,
session journal and per-book history remain separate work.

## Reader flow

On X3/X4 and X4 Pro, Home shows Continue Reading, a selectable statistics
panel, recent books and a compact Library / Catalogs / More row. Opening
Statistics replaces Home so the cover/UI allocations are released before
network work. Back returns to Home and reloads its saved summary. Home never
starts Wi-Fi. X4 Pro uses the shared touch routing and omits physical-button
hints; physical navigation remains available.

Statistics opens the saved account snapshot. Update explicitly opens the
existing Wi-Fi selection flow if necessary, then requests a new summary.
A reader without a usable YBX token/manifest goes to Catalogs (or Settings
when plugins are disabled) before starting Wi-Fi. The screen disconnects Wi-Fi
on exit only if it initiated that connection. A pre-existing connection is
left to its original owner. Wi-Fi child-return edges are consumed before
handling new actions. No worker or background polling is added. Controls are disabled during the
synchronous refresh; this screen does not promise cancellation inside blocking DNS/TCP.

## Plugin and cache contract

The summary contract was introduced in SD plugin1.8.0. The bundled plugin is
now **1.9.0**, adding the achievements request. Its `device.json` supplies
`ybx.reading_summary`, a GET request object using the existing URL/headers
vocabulary and `{token}`. The bundled endpoint is
`/api/v1/crosspoint/reading/summary?limit=1`. Firmware requires an HTTPS URL;
certificate verification still has the inherited transport limitation.
Only the token template is supported in this native request; provider account
sessions stay on YBX. Ordinary/stock plugin compatibility is unchanged.
Existing readers need the updated SD plugin, not just the firmware binary.

The native cache is `<ybx plugin directory>/reading-summary.json`:
schema1, a SHA-256 binding to the current reader token, the bounded summary
fields/week cells and `fetched_at`. No raw token is written to this cache.
Relinking or token rotation invalidates a previous token's cache. Replacement
uses the existing recoverable pending/backup transaction. It never writes
`/.crosspoint/reading_stats.json` or merges cloud observations into a local
session journal.

Responses are capped at8KiB, cache at4KiB, and seven account-local calendar
days. Null/invalid metrics remain unknown; observed zero remains zero. The
chart uses the account's ISO dates, including month/year boundaries, and
rejects repeated or out-of-week cells. A malformed/truncated reply does not
replace the saved summary. A successful reply with a failed SD save can be
shown for the current visit, with a warning that it was not saved.

The snapshot's account date stays visible on Home and Statistics. Statistics
also shows the fetch time in UTC when the trusted reader clock was available;
otherwise no fetch time is invented. This is cached data, not a live total.
Update uses a12-second cooperative timeout; inherited synchronous DNS/TCP can
exceed it. A failed refresh retains the last summary; unauthorized responses
retain that token-bound offline history and direct the user to reconnect.
No authorization or live-status claim is inferred from the saved data.

## Verification and device acceptance

Host tests cover nullable/range-checked metrics, account calendar boundaries,
duplicate/invalid dates, truncated replies, token-bound cache schemas, Home
navigation and both screen layouts across panel rotations. Firmware builds
and independent reviews are recorded with the implementation artifacts.
Host checks do not establish runtime heap, radio lifetime or touch behavior.

On each device, check all four rotations, empty/recent libraries, button
remapping, tap/press/hold, returning from Wi-Fi success/cancel, offline cached
entry, unpaired/expired tokens, token rotation, SD write failure and long
translated labels. Confirm Home stays offline, refresh closes only its own
Wi-Fi connection, failed replies preserve the cache, and memory returns after
repeated Home/Statistics/catalog transitions. X4 Pro release assets, USB
branding and hardware acceptance remain separate from enabling its new Home.
