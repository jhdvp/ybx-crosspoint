# YBX Reader firmware

This is the YBX firmware fork of CrossPoint Reader for Xteink X3/X4, with
initial X4 Pro build/UI coverage pending hardware acceptance.
The web panel retains the upstream English interface. Reader screens use the
selected device language; the language-invariant YBX brand has English fallback.

The initial integration is based on upstream `develop` at
`a790b144` (2026-10-27), newer than the published `2.0.0rc` tag.
The complete upstream history and MIT license remain intact.

## Product and compatibility

YBX branding covers boot/default sleep, the web panel, device network names,
About/version information and firmware release assets. The mark is the same
three-line symbol used by ybx.app, rendered in monochrome for the reader.

Settings, progress, caches and existing plugin tokens retain their
`/.crosspoint/` paths. The browser plugin API remains `window.CrossPoint` and
the firmware compatibility family remains `crosspoint`: these are protocol
identifiers, not a claim to be an official upstream release. This avoids a
credential or library migration solely for branding.

Firmware updates query **jhdvp/ybx-crosspoint**, never upstream's release feed.
Published assets use `ybx-<version>-<board>.bin`; the combined X3/X4 suffix is
`x3-x4`. Both models use the compiled compatibility tag `x4`, so no separate
`-x3` asset is expected. OTA follows stable releases only; preview-to-preview
updates use SD installation. Release tags are the configured numeric version
(optionally prefixed by `v`), with literal `rc` appended for preview builds. The release workflow and OTA lookup use the same names. The fork owner
must publish and maintain these releases. Until a compatible release exists,
checking for an update cannot install an upstream image.

## YBX catalogs

The matching SD plugin is under `sd-card/plugins/ybx`. Use `python scripts/install_ybx_plugin.py /path/to/card` to install it. The
installer preserves an existing `token.json` and `config.json`, detects the
legacy plugin location, and replaces source files atomically. It stops if two
YBX installations would make the target ambiguous. For manual copying, preserve
both token and config files.
The firmware must advertise `catalog.lists.auth` before installing this plugin.

The list index uses an explicit `browse.lists_headers` authorization header.
The server builds its response from the current account's connected Yandex,
WB and LitRes sessions. After connecting or unlinking a service, leave the
catalog and open YBX again. No connected sources gives an empty book list.
An invalid reader token offers device-code sign-in; a broken provider session
is a separate error and must be reconnected in the YBX cabinet.

Catalog thumbnails are not part of the current firmware plugin UI. Downloaded
EPUBs use their package cover in the reader library. A sampled valid file does
not prove rendering on every hardware panel or every publisher's edition.

## Build and first hardware check

Use the pinned FreeInk SDK submodule for X3/X4. The RC environment requires
a build identifier, as in CI:

```sh
CROSSPOINT_RC_HASH="$(git rev-parse --short=7 HEAD)" pio run -e gh_release_rc
```

The displayed version is `<version>-rc+<identifier>`; the GitHub preview tag
uses `<version>rc`. Local uncommitted previews use a source-patch identifier
recorded in BUILD.json.
Do not erase the SD card or `/.crosspoint/`: it contains settings and progress.
Back up the card and existing firmware before the first device installation.

Check boot and default sleep in all four orientations, the YBX About version,
Wi-Fi transfer at `ybx.local` (or the displayed IP), and the web header/favicon.
Check zero sources, Yandex only, WB only, LitRes only and combinations; after
unlinking, reopen YBX and confirm the folder disappears. Test reader token
expiry separately from provider reconnection. Download one EPUB from each
linked provider and check its cover in the library. Confirm OTA reports the
fork channel and never offers a CrossPoint upstream asset.

Host tests and a successful build do not establish device heap, battery or
rendering behavior. Hardware acceptance and ownership of the fork's releases
remain with the maintainer; no automatic flashing is performed.

## Inherited transport limitation

The pinned upstream SecureNet transport uses `setInsecure()` in PluginHttp and
HttpDownloader. HTTPS encrypts traffic but does not authenticate the server
certificate. This fork has not changed that transport policy; do not describe
these requests or OTA downloads as certificate-verified. CA/hostname verification
is a separate release-hardening item requiring SDK work and device memory/time
validation. Do not treat this preview as a security-reviewed public release.

The reader sends its scoped YBX token and reading events to YBX. Provider OAuth
tokens and LitRes/WB sessions remain on the server. A reader token can authorize
catalog and owned-book requests, so it still needs protection in transit.

## YBX home on X3/X4 and X4 Pro

The home follows the YBX clients: a branded header, the most recently opened
local book with its saved reading percentage, up to two other recent books,
and a compact Library / Catalogs / More footer. Files, file transfer and
settings live in the More popup. Percentages are hidden
when no saved position is available. Recent books are not a cloud reading
shelf. Home reads a token-bound account summary from SD without connecting Wi-Fi.

Use the mapped navigation buttons to move through books and actions, Confirm
to open, and Back to resume the latest book. Empty devices start at Library.
Catalogs appears only when Plugins is enabled or an OPDS server is configured;
it opens the existing Plugins/OPDS flow. Select a recent cover directly; its
caption is plain text and saved progress, not a second title button. Other board
profiles keep their home. X4 Pro uses shared touch routing and no physical-button hint strip.
The firmware uses its existing UI fonts rather than bundling desktop fonts.

Hardware acceptance: check empty/one/three books, a deleted recent file, missing
or corrupt cover, unknown progress, long translated titles, all four rotations,
remapped front/side buttons, returning from each action, and repeated Home/book
transitions. Record heap/latency and confirm no radio activation on Home.

## YBX sync setup after pairing

A reader that signed in to YBX (QR device code in the plugin catalog) configures
its built-in KOReader Sync by itself: `src/util/YbxKosync.cpp` writes the sync
server `https://my.ybx.app/api/v1/crosspoint/kosync`, username `ybx` and the
reader token from the plugin's `token.json` as the password, plus content-hash
document matching, smart behaviour and metadata, through the existing
`KOReaderCredentialStore`. Nobody types a URL or a password, and the browser
plugin page is no longer part of the flow. Provider OAuth stays on the server.

The module owns no protocol, timer, queue or connection: hashing, requests,
retries and the pull/upload screens stay in `lib/KOReaderSync`. It reconciles at
three moments only — after a plugin token is written, when the sync settings
open, and before the reader starts a sync — each of which reads two small plugin
files and writes storage only when something changed. It never rewrites a sync
server the reader typed by hand: that case offers a "use YBX" row instead, and
editing the settings after provisioning — URL, username or only the password —
hands the server back to the reader. A hand-over that left the YBX-derived
username and key as the password drops them with it, so the reader key is never
sent to a non-YBX host. While YBX credentials are active the "Sign up" row is
disabled: the reader key is not an account password.
The switch in Settings → CrossPoint Sync is the opt-out; turning it off clears
only what this module (or the browser page it replaced) wrote.

Settings shows the switch, the verdict of the last transaction with its age, and
the pending-mismatch state. Like the other YBX clients, the reader applies a
server position silently only within one percentage point and asks above it;
the local position remains the record, so an offline attempt or a declined jump
never rolls anything back, and the next sync reconciles again.

Hardware acceptance: pair on a device and confirm the sync rows and that a
position reached in another YBX client (phone/desktop) opens the right page in
the reader, and the reverse; check Yandex, WB and LitRes copies plus a locally
normalized EPUB; then verify offline retry, opt-out, re-pair after an expired
token, and that a hand-configured server is never overwritten.

The broader native-client scope is tracked in [the parity matrix](docs/ybx-client-parity.md)
and [the statistics adaptation plan](docs/ybx-reading-stats-plan.md). These are
implementation plans. The first native account-summary slice is described in
[Native statistics](docs/ybx-native-statistics.md); achievements and local history
synchronization are still pending.

Current implementation checks and open release blockers are recorded in
[the validation status](docs/ybx-validation-status.md).
