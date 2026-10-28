# Plugin events and the book metadata sidecar

Two firmware surfaces that let SD plugins react to what happens on the reader
and attach service data to books, while keeping all service knowledge out of
the firmware. They compose with the other plugin surfaces documented in
`sd-plugins.md`.

- **Events**: the firmware records a small whitelisted set of moments
  (a reading session ended, the device is going to sleep, ...) into a per-plugin
  queue on the SD card, and later replays them through HTTP request templates
  the plugin declared in its `device.json` — no browser, no plugin code
  running on the device.
- **The sidecar**: a small JSON file next to a book
  (`<book path>.meta.json`) where plugins record fields like a service book
  id at download time. Its fields ride along with KOSync progress uploads and
  are available to event handlers as substitution variables.

## Why delivery is deferred

Plugin event handlers need the network, and most events fire with WiFi down
(leaving a book, entering sleep). So `emit` only appends one JSON line to the
plugin's outbox (`<plugin dir>/events.jsonl`) — an SD append, nothing more —
and the queue is drained through the declared requests whenever the device is
already online: on any WiFi connection (the selection/auto-connect completion
in `WifiSelectionActivity`), when the File Transfer web server comes up in Join
Network mode, or on the way into sleep. Delivery is at-least-once and in order;
every queued line carries a unique `id` (a per-boot nonce plus a counter, so it
never repeats across queued events or reboots) — dedupe on `{event.id}`
server-side if it matters.

Event subscriptions need a plugin folder name of at most 23 bytes, with the
full folder path (root included) within 63 bytes; a plugin past either limit
is logged and not subscribed.

`sleep.enter` gets special timing: it exists to act before the chip powers
down (a fresh sleep image, a pre-sleep progress push), so the sleep path
actively brings WiFi up to deliver it at sleep entry instead of waiting for
the next online session. See "sleep.enter delivery" below.

## The event whitelist

Event names are a compatibility promise: they are semantic ("a reading session
ended"), never internal activity names, and manifests can only bind to names
on this list. Unknown names are ignored with a log line, so a manifest written
against newer firmware degrades gracefully on older firmware.

| Event             | Fires when                                             | Variables                            |
| ----------------- | ------------------------------------------------------ | ------------------------------------ |
| `reader.open`     | A book is opened (EPUB/TXT/XTC)                        | `book` (SD path)                     |
| `reader.exit`     | The reader is left (home, sleep, another activity)     | `book`, `percent` (0-100 whole-book) |
| `reader.session`  | A reading interval ends: exit, sleep, restart, or recovery at boot after a crash | fields documented below |
| `book.downloaded` | An on-device catalog download completed                | `path`, `title`, `plugin`            |
| `sleep.enter`     | The device begins entering deep sleep                  | `book`, `percent` when sleeping out of a reader, else none |

`sleep.enter` carries the reader snapshot because sleeping straight out of a
book is the common flow, and the reader's own `reader.exit` fires after the
sleep-time drain: binding a progress-sync handler to both events means the
current position is pushed on the sleep connection, not the next one.

### `reader.session` contract

All values are strings in the event's `vars` object:

| Variable | Meaning |
|---|---|
| `book` | SD path |
| `document` | binary KOSync partial-MD5, exactly 32 lowercase hex characters |
| `start_time` | trusted Unix epoch at the start of the first counted dwell |
| `end_time` | compact active end, exactly `start_time + duration_seconds` |
| `duration_seconds` | sum of counted active dwell seconds |
| `start_progress_bp` | first counted whole-book progress, 0..10000 basis points |
| `end_progress_bp` | monotonic-clamped final progress, 0..10000 basis points |
| `progress_scale` | literal `10000` |

Only successful ordinary page turns close an interval, in either direction.
A displayed page contributes when it remains visible for at least 2 seconds;
one interval is capped at 1800 seconds. Chapter skips, failed turns, and
shorter displays are excluded. The page still on screen when reading stops
(exit, sleep, restart) contributes its trailing interval through the same
2/1800-second bounds. A session shorter than 5 active seconds is not reported:
it is an accidental open or a quick flip, not reading. A redraw of the same
page (full or forced refresh) does not reset the interval. A screen pushed on
top of the page — the reader menu, dictionary, bookmarks, clipping list,
KOSync or the frontlight panel — does: its dwell closes when the page loses
focus and the render after the screen returns opens the next one, so time
spent in those screens is not reading time.

`start_time` is `0` when the device clock was never set; the session is still
reported, and the envelope `event.ts` carries the same uncertainty. Both
`start_time` and `end_time` are `0` in that case. A clock that arrives
mid-session (SNTP after boot) still places the start: it is back-computed from
the first counted dwell whose clock was known, so a session that began before
the sync is not reported with `start_time` `0`. EPUB retains basis-point
precision; TXT/XTC may report whole-percent steps. The document hash is
calculated lazily only when an emit-worthy summary and a subscriber exist.

`end_time` is deliberately not the wall-clock time of the last interaction.
Idle gaps are removed, so it always equals `start_time + duration_seconds` and
is greater than `start_time` for every emitted session with a known clock. The
envelope `event.ts` remains the actual emission time.

The pre-sleep activity hook queues and resets this event immediately before
`sleep.enter` and the sleep drain. Normal reader teardown calls the same
idempotent flush as a fallback, so direct-from-reader sleep queues it exactly
once and can deliver it on that same sleep. The explicit restart paths flush
first as well.

### Session identity and crash recovery

The event id is allocated once, before the session's first checkpoint or
flush, and is written to a small checkpoint
(`/.crosspoint/reader-session.journal`) at most once a minute while reading. If the device crashes, loses power, or
restarts without a flush, the next boot queues the checkpointed session with
that same id and deletes the checkpoint. If the checkpoint could not be
deleted, or the event was only queued for some subscribers, the same id is
queued again: a server that keys its store by `(device, event id)` (as YBX does)
ignores the duplicate instead of counting the reading time twice.

The 5-second minimum applies when the session ends; a crash can therefore
recover at most the last checkpointed minute of a session that would have been
reported anyway. Every reader — EPUB, XTC and TXT — writes that checkpoint
from its own loop, so a power cut costs at most the current minute in any
format.

A checkpoint is only replaced once the session it holds has been queued: if
that delivery fails, the record stays on the card for the next boot and the
session still in RAM does not overwrite it. A flush clears the checkpoint only
when the record carries its own session id, so the second flush of the sleep
path (the activity teardown that follows `prepareForSleep`) cannot delete the
retry record the first flush just wrote. A checkpoint that fails to be written
is retried on the next minute, not on every loop pass.

A generic consumer can expand one summary into two distinct points without
firmware knowing the destination schema:

```json
[
  {"time": "{event.start_time}", "progress_bp": "{event.start_progress_bp}", "duration": "{event.duration_seconds}"},
  {"time": "{event.end_time}", "progress_bp": "{event.end_progress_bp}", "duration": "0"}
]
```

The distinct compact timestamps avoid a uniqueness collision even when the
two progress values are equal. Preserve at-least-once delivery and deduplicate
replayed points by the consumer's natural document/time key, or by
`{event.id}` when the clock was unknown (`start_time` is `0`).

## Declaring handlers in `device.json`

```json
"events": {
  "reader.exit": {
    "request": {
      "method": "POST",
      "url": "{cfg.server}/api/progress",
      "headers": { "Authorization": "Bearer {token}" },
      "body": "{\"book\":\"{event.book}\",\"pct\":{event.percent},\"id\":\"{meta.bookfusion_id}\",\"ts\":{event.ts}}"
    },
    "toast": "Synced {event.book}"
  },
  "sleep.enter": {
    "download": { "url": "{cfg.server}/daily.bmp", "dest": "/sleep.bmp" }
  }
}
```

One handler per event. A handler is either:

- **`request`** — `url`, `method` (default `POST`), `headers`, `body`. The
  response is an acknowledgement: any 2xx counts as delivered, the body is
  discarded (8KB cap).
- **`download`** — `url`, `method` (default `GET`), `headers`, `dest`. The
  response streams straight to `dest` on the SD card (1MB cap, never buffered
  in RAM). `dest` must be an absolute path without `..`. The example above
  fetches a fresh sleep image: the sleep-entry drain runs before the sleep
  screen renders, and the CUSTOM sleep mode reads `/sleep.bmp` first, so the
  fetched image appears on that very sleep. Downloading a book file (`.epub`,
  `.txt`, `.md`, `.xtc`) also marks the Library index stale, so the book
  appears the next time the Library opens. Hidden files and folders don't
  mark it.

Optional per handler:

- **`toast`** — a template shown as the standard popup after the request
  succeeds, when a screen is available (sleep entry, web-server session).
- **`require_meta`** — a sidecar key, e.g. `"require_meta": "bookfusion_id"`.
  The event is queued for this handler only when the event's book (`book`, or
  `path` for `book.downloaded`) has a metadata sidecar where that key holds a
  non-empty string or a whole number (decimals and `true`/`false` do not
  match). Other books' events are not queued at all, so they cost no outbox
  space, no request, and no WiFi bring-up at sleep. An event without a book
  (`sleep.enter` outside a reader) is not queued, and neither is one whose
  sidecar cannot be read. The sidecar is read when the event fires, so write
  it before the book is opened. A handler whose `require_meta` is not a
  string is ignored, so a wrong type cannot turn the filter off.
- **`connect`** — normally the drain only runs when the device is already
  online. `"connect": true` opts a handler into WiFi bring-up at **sleep entry
  only**. Subscribing to `sleep.enter` sets that opt-in for you, so a
  `sleep.enter` handler never needs the key (it is still accepted, and ignored
  as redundant). Other events must declare it explicitly.

## sleep.enter delivery

At sleep entry the device drains queued events for any opted-in handler that has
something queued, not only handlers subscribed to `sleep.enter`. So a
`reader.session` handler that declares `"connect": true` is delivered on the same
sleep that ends the reading turn.

If WiFi is not already up, the device brings it up for the drain: with battery at
least 20% and a saved credential for the last-connected network, it shows a
popup, joins with a 10-second deadline, drains, and proceeds to sleep. Sleep is
never blocked on the network — a failed join, low battery, or a mid-drain failure
just sleeps with the previous image, and the queued events retry on the next
drain like any other (at-least-once; a handler whose action only made sense for
that particular sleep should tolerate or ignore the late retry, e.g. by checking
`{event.ts}`). Other events already queued for the same plugin (`reader.exit` and
friends) ride along in the sleep-time drain.

Auth reuses the catalog vocabulary from the same `device.json`: `{token}` is
read from the declared token file, and on a 401/403 a `"password"`-type auth
section mints a fresh token and retries once — same behavior as catalog
browsing.

## Substitution variables

Templates in `url`, `headers`, `body`, `dest`, and `toast` substitute:

| Variable       | Source                                                          |
| -------------- | --------------------------------------------------------------- |
| `{token}`      | The plugin's token file (see `device.json` `token` section)      |
| `{cfg.KEY}`    | The plugin's flat config file (`config` section)                 |
| `{event.NAME}` | The event's variables (see the whitelist table)                  |
| `{event.ts}`   | Unix time when the event fired; 0 if the clock was never set     |
| `{event.id}`   | Unique event id, stable across delivery retries (dedupe key); for `reader.session` also across a reboot that recovers the checkpoint |
| `{meta.KEY}`   | The book's metadata sidecar, for events that carry a `book` (or `path`, for `book.downloaded`) |

Values are inserted as they are. Before a request is sent, spaces, control
characters, and non-ASCII bytes in its URL are percent-encoded; `&`, `?`, `#`,
`+` and `/` inside a value are not, and events have no `_enc` forms.

Unmatched variables substitute as nothing was replaced (the literal `{...}`
text is sent), so servers should tolerate or reject malformed values.

## The book metadata sidecar

Convention: a flat JSON file next to the book, `<book path>.meta.json`:

```json
{ "bookfusion_id": "36835", "source": "bookfusion" }
```

**Writers.** Anything that can already write small SD files:

- the on-device catalog's `sidecar` mechanism (`device.json` download section)
  at download time,
- browser-side plugin JS via `POST /api/plugin-fs`,
- a computer, by hand — it is just a file.

**Namespace your keys.** All plugins share one sidecar per book and the last
writer of a key wins. Use `bookfusion_id`, not `id`.

**Consumers.**

1. **KOSync progress uploads.** When *Send book metadata* is enabled in the
   KOSync settings, the sidecar's fields are merged verbatim into the
   `metadata` object of every progress upload (the reserved keys `filename`,
   `title`, `authors` cannot be overridden). Third-party KOSync servers ignore
   unknown fields; a custom sync server can route progress to a service using
   e.g. `metadata.bookfusion_id` — the firmware never learns what any key
   means.
2. **Event handlers**, via `{meta.*}` as above.
3. **Handler filters**, via `require_meta` (see above).

Fields are flat strings, numbers, or booleans with a 2KB read cap. KOSync
uploads carry them with their JSON types; `{meta.*}` templates see strings
and whole numbers as text, and decimals and booleans as empty. Null and
nested values are ignored. Structured service data
belongs on the service's server, keyed by the id in the sidecar.

## Delivery semantics and limits

- Ordinary events are delivered at least once, in order per plugin, within
  the outbox size limit below. The drain stops ordinary delivery at the first
  failure; undelivered events wait for the next drain.
- A `sleep.enter` GET download may opt into `"delivery": "latest"` alongside
  its `download` object. One attempt from the drain budget is reserved for the
  newest queued sleep snapshot, after ordinary events. It is attempted even
  if an ordinary event failed. Only after that download succeeds are older
  queued sleep snapshots removed as superseded. Failed downloads retain all
  snapshots. Other event types and POST handlers cannot opt in.
- The budget counts handler attempts, including failures. Authentication
  refresh and its retry can make more than one HTTP call per handler. This
  is a request-count limit, not a wall-clock sleep deadline. Across plugins,
  the shared budget and existing subscription order still apply.
- Sleep entry uses a 15-second cooperative drain budget after the existing
  Wi-Fi join window (up to 10 seconds). Ordinary handlers share a 2-second
  budget per event; an opted-in latest snapshot has up to 8 seconds. Token
  refresh and retry share the event budget. Online drains keep the normal
  30-second request timeout. Synchronous DNS/TCP connect and SD I/O may overrun
  the cooperative budget; this is not a hard upper bound on sleep latency.
- Queue replacement writes and closes `.pending`, parks the previous queue
  as `.bak`, then renames the complete replacement. Before append, inspection
  or delivery, a missing queue is restored from `.bak`. An existing queue is
  authoritative; uncommitted `.pending` files are discarded, never promoted.
  A failed restore leaves the backup for another attempt. This handles failed
  operations and interrupted transaction boundaries; physical FAT/SD corruption
  still requires device testing and cannot be excluded by host tests.
- Before appending, the firmware checks the final line boundary and truncates
  only a partial trailing event (at most 1024 bytes). A read/truncate failure
  stops that append; the next event is never glued to a known incomplete line.
- When the outbox is at its size limit, the oldest whole events are dropped
  first, so a long offline period keeps the newest history instead of losing
  everything. The line being appended is never split.
- A streamed download is accepted only after a complete HTTP response, complete
  writes and successful file close. A close failure keeps the old sleep image
  and the queued event for retry.
- A corrupt queue line, or a line whose event no longer has a handler, counts
  as delivered — a manifest edit cannot wedge the queue.
- The subscription table is rebuilt at boot. A plugin installed mid-session
  starts receiving events after the next restart.

| Limit                          | Value                          |
| ------------------------------ | ------------------------------ |
| Outbox size                    | 4KB per plugin, then oldest whole events are dropped first |
| Event line                     | 1024 bytes                     |
| Request response               | 8KB (discarded)                |
| Download                       | 1MB, streamed to SD            |
| Subscribed plugins             | 8                              |
| Handler attempts per drain     | 4 (backlog continues next drain) |
| Sidecar / config / token files | 2KB each                       |

## Worked example: hands-free progress sync

A sync plugin writes a service id to the sidecar when it fetches a book, and
binds a request to `reader.session` with `"connect": true`. The user reads
offline for a week and
presses sleep: the device queues the event with the current percent, joins the
saved network for a few seconds, POSTs
the compact active-session summary to the plugin's server, shows
"Synced", and goes to sleep. Nothing was tapped, no browser was involved, and
the firmware contains no mention of the service.
