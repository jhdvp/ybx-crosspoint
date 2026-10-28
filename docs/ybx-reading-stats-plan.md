# Reading statistics on the YBX home

Source review: 2026-10-28. The owner requested a compact footer and reading
statistics instead of repeated title buttons. The home prototype now has a
single Library / Catalogs / More row. Recent covers are selected directly;
their captions are plain text, with saved percentages when known. No statistics
values or working statistics screen are claimed by this prototype.

## Proposed home

Keep the current book and Continue Reading first. Use the space recovered from
the five-button grid for Today, This Week and Streak plus seven daily bars.
Opening that section should lead to a native statistics page. Unknown values
must stay unknown; offline cached data must show its age. Do not fetch on every
Home repaint, and do not add an inactive statistics button as a substitute.

## vCodex: adapt the statistics module

Reference: [franssjz/cpr-vcodex](https://github.com/franssjz/cpr-vcodex), snapshot `6bde1d6700f7b89cc060e93c4a8cab45ce4643c7`. It is MIT licensed. Relevant
modules include ReadingStatsStore (1,889 lines in the inspected snapshot),
ReadingStatsAnalytics, ReadingStats/DayDetail/Heatmap/Profile activities,
ReadingStatsImport, ReadingSessionLog, JsonSettingsIO and TimeUtils. These are
coupled to settings, activity routing, logging and the older reader lifecycle;
they are not a drop-in plugin.

At review time, comparison with our upstream base `a790b144` (the parent of our YBX fork
commits `1043a2d0` / `e728f5dd`) was diverged: vCodex was
536 commits ahead and 133 behind. Keep the current CrossPoint 2 base and port a
bounded statistics slice. Do not merge the whole fork, its hardware/recovery
stack, themes or unrelated applications just to obtain a home widget.

## Reuse YBX observations, avoid a second clock

Our ReaderSession already records qualified page dwell (2 to 1,800 seconds per
interval, page turns in either direction, the trailing interval at exit, and a
5-second session minimum). vCodex has another session state, a heartbeat, a
three-minute threshold for its session counter, and a goal-based streak. Blindly
running both collectors would create conflicting definitions. Adapt the analytics/store to consume one
completed session event, with a stable ID shared by local persistence and YBX
upload. Agree on idle time, midnight splitting, timezone and the streak definition
before exposing totals. Use existing trusted-time synchronization; an uncertain
offline day must not silently become today.

The YBX backend already exports stored history in vCodex v6 format at
`/api/v1/crosspoint/reading/vcodex`. This is a compatibility export, not a live
bidirectional sync implementation. The current plugin can write it to
`/.crosspoint/reading_stats.json`, which is also vCodex's native live store.
After a native port this path must not be overwritten by cloud snapshots.
Import through a staging file, preserve a backup and merge using stable book
identity/session receipts. A repeated import and a round-trip of this device's
already-uploaded sessions must not double the reading time. Cloud synthetic
`/ybx-cloud/` paths alone do not identify the local EPUB.

## ESP32-C3 boundaries and work sequence

A server export can contain up to 2,048 books / 4 MiB. Do not deserialize the
whole cloud history into X3/X4 RAM. vCodex itself has releaseMemoryForNetwork /
reloadAfterNetwork because statistics compete with TLS allocations. Retain a
small bounded home summary and a limited journal; stream longer history from
SD. Measure the largest free block and opening/TLS/sleep peaks on both devices.

1. Fix the existing sleep FIFO blocker and verify session delivery first.
2. Define one local session journal, IDs, time semantics and safe cloud import.
3. Adapt native store/summary and statistics view; keep the new home composition.
4. Add calendar/heatmap, per-book breakdown and optional goals/achievements in
   separate changes after the core totals reconcile with YBX.
5. Test reboot/offline/midnight, import twice, cloud round-trip, large history,
   damaged file, memory release around TLS, and real X3/X4 sleep/wake.

This is a source-based implementation plan. No vCodex code was incorporated or
built in this change, and no vCodex hardware-validation claim transfers to YBX.

## Implemented slice (28 October 2026)

The first step of the sequence above is in place on the YBX fork:

- One session source: finished `reader.session` intervals feed a device-local
  store (`/.crosspoint/reading-stats.bin`, `/.crosspoint/reading-books.bin`)
  next to the existing plugin event, with the same session id for dedupe. No
  second timer or second session definition was added.
- A declarative dashboard contract: manifests may set
  `dashboard.source: "reading-stats.v1"` and pages made of `metric`,
  `week_bars`, `heatmap_28` and `book_list` widgets. The YBX manifest declares
  «Ваше чтение», «Неделя», «Календарь» and «Книги»; labels are rendered
  verbatim from the manifest.
- The firmware advertises `stats.native.v1` and `ui.dashboard.v1`, so the
  catalog no longer filters a statistics plugin that requires them.
- Local semantics: local-midnight splitting, local-day ordinals, no-data days
  distinct from zero, undated sessions when the clock is unknown, and a
  freshness footnote.
- Deliberately not done yet: server streak/goal/achievements (needs a capability
  and data-source decision), cloud history import, and replacing the vCodex
  export. The native store does not use `/.crosspoint/reading_stats.json`; the
  user-initiated export button still writes it for other firmwares.

See [implementation status](ybx-validation-status.md#native-reading-statistics-fw-native-stats-branch)
for limits and the owner's hardware checklist.
