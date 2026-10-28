# YBX Reader — client parity baseline

Owner request, 2026-10-28: native YBX on Xteink, including achievements and all
working functions of the other YBX clients. Audio remains excluded.

Reference snapshot: ybx-apps 0e24398448a8612871b8f20dfa6f8150bb544a97,
Classic ybx_inkview_app.c, Desktop qml/Main.qml and the pages it actually routes.
Source presence does not establish device acceptance or a release.
Firmware baseline: compact Home/sleep changes are committed in344185ee;
SDK44427f8d is unchanged. The native account-summary slice is a subsequent
local candidate. This matrix records the remaining implementation scope.

| User flow | Existing firmware reuse | YBX integration still needed |
|---|---|---|
| Home / continue / local progress | RecentBooksStore, BookProgress, native reader | Home with cached account-summary panel; hardware acceptance pending |
| Personal library / reading/wanted/finished | PluginCatalogActivity, authenticated dynamic lists | Native nested provider/shelf/series navigation; shared cloud/local identity |
| Search / pagination | Existing plugin/native OPDS search and paging | Full provider filters and source-state UX acceptance |
| Book details / cover / author / series / actions | Existing EPUB/library cover path and generic details | Native thumbnails, real book actions, provider alternatives; no fake Fetch actions |
| On Reader / downloads / delete | LibraryIndex, FileBrowser, bounded downloader; catalog shelf states, shelf menu, offline Home→More folder (this branch; firmware and host suites build, not device-tested) | Device acceptance of states, safe deletion and re-download |
| EPUB reading / TOC / bookmarks / dictionary | Existing reader engine and native UI | Verify the same downloaded files, image books and preserved metadata |
| Reading position across clients | Existing KOSync, server position conversion | Native pairing-to-KOSync setup, state/errors and mismatch UX without browser JS |
| Reading summary / month / day / year | ReaderSession events; FreeInkUI widgets | Summary/cache and weekly chart implemented; month/day/year views and one session ledger remain |
| Reading position across clients | Existing KOSync, server position conversion | Native pairing-to-KOSync setup, opt-out, outcome status and mismatch consent implemented; device acceptance pending |
| Reading summary / month / day / year | ReaderSession events; FreeInkUI widgets | Bounded native account model/cache, views and one session ledger |
| Book-level reading statistics | Existing book identity / metadata sidecars | Native per-book stats and reliable shared identity |
| Goals / streaks | Server profile data | Display actual shared goals and server streak semantics, unknown != zero |
| Received and pending achievements | Existing YBX achievement API/model contract | Native awards screen, progress/date, offline cache and pagination |
| Reader pairing / unlink | Existing device-code auth and scoped token | Native account status/repair/unlink and consentful settings flows |
| Connected sources / link / disconnect | Existing server-owned credentials and provider lists | Native source management with phone/QR for external provider sign-in |
| Settings / language / theme / sync / folders | Firmware Settings and UI translation system | YBX-specific controls/labels and client-parity validation |
| Updates / About | YBX OTA channel and version branding implemented | First accepted release, compatibility, recovery and update-state testing |
| Support / diagnostics | Existing server support-log API; firmware logs | Native explicit redacted-log submission and receipt, no automatic private-book upload |
| Offline / expired token / empty / failures | Existing fallback routes and queue | Consistent cached/unknown/stale presentation, bounded network wait, sleep FIFO and TLS fixes |

Use the portable YBX client protocol/model where practical. The current desktop
history client delegates to core/ybx_reading; that header also pulls in the
SQLite cache API and allows a96KiB cache/40 awards, so it cannot simply be linked
unchanged into ESP32-C3 as a resident model. Separate the bounded protocol/model
from platform storage first; no private parser/layout/sync forks inside a UI.

Canonical achievements/goals/history belong to the YBX account. vCodex is an
MIT reference for native statistics and optional offline aggregation, not a
second independent account or parallel rewards system. Adapt the current
ReaderSession once; never run a second independent reading timer.

Work order: stable session/sleep/TLS foundation; bounded account/profile model;
summary + history + awards; native source/account/sync flows; book/library
parity; device acceptance. Reuse completed source-level work and keep hardware
acceptance distinct from code presence.

Implementation update (28 October): the first native account-summary slice
adds the Home panel and a Statistics screen with explicit refresh, a token-bound
SD cache and weekly chart. X4 Pro uses the shared YBX Home/touch routing. This
does not complete calendar/day details, awards/goals or local-session sync.
See [Native statistics](ybx-native-statistics.md) for the precise boundaries.
