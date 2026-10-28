# YBX Reader firmware

This is the YBX firmware fork of CrossPoint Reader for Xteink X3/X4.
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

SDK fixes not yet released upstream are carried as reviewed patch assets and
applied by a build script; see [the SDK patch plan](docs/ybx-sdk-patches.md).

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

## Transport security

Requests that carry the reader token or download a payload verify the server
certificate chain and host name over wolfSSL: the plugin HTTP client, catalog
and book downloads, OTA release check and install, the font catalog, and the
web server's resumable fetch. Verification uses a small built-in CA bundle
(ISRG Root X1, ISRG Root X2, Sectigo Public Server Authentication Root E46 and
its USERTrust ECC issuer, about 4.9 KB of PEM in `src/network/TlsRoots.h`). It
covers the YBX hosts, the GitHub API/release-asset hosts and the font catalog.
A user-added OPDS catalog works only when its chain reaches one of those roots;
otherwise the request fails with a certificate error and is not retried
insecurely.

Before any https request the firmware checks the TrustedTime floor. If the
clock is unset it makes one bounded SNTP attempt; when time still cannot be
established the request is refused with a message asking the user to connect
Wi-Fi, rather than skipping the certificate-date check. Redirects never step
down from https to http, and Authorization/cookie headers are withheld when a
redirect leaves the starting origin.

Known exceptions, outside the token/download scope: KOReader sync and clippings
sync still call `setInsecure()` because they talk to user-supplied servers of
unknown CA; the SDK's OPDS implicit-auth flow, not wired into this fork, does
the same. Their traffic is encrypted but not authenticated — treat those
servers as trusted.

The SDK change is delivered as a hash-guarded pre-build patch
(`scripts/patch_securenet.py`, `scripts/securenet_patches/`) because the pinned
FreeInk SDK transport offers only `setInsecure()`/`setCACert()` without host
name checking. The submodule pointer is unchanged; after a build
`freeink-sdk` shows four modified files until the change is upstreamed. The CA
bundle's roots, fingerprints, validity dates and re-derivation command
(`openssl s_client -showcerts`) are documented in `TlsRoots.h`; re-check them
when a root approaches expiry.

## YBX home on X3/X4

The home follows the YBX clients: a branded header, the most recently opened
local book with its saved reading percentage, up to two other recent books,
and a compact Library / Catalogs / More footer. Files, file transfer and
settings live in the More popup. Percentages are hidden
when no saved position is available. Recent books are not a cloud reading
shelf; Home does not connect Wi-Fi or claim an account/sync status.

Use the mapped navigation buttons to move through books and actions, Confirm
to open, and Back to resume the latest book. Empty devices start at Library.
Catalogs appears only when Plugins is enabled or an OPDS server is configured;
it opens the existing Plugins/OPDS flow. Select a recent cover directly; its
caption is plain text and saved progress, not a second title button. Other board
profiles keep their home.
The firmware uses its existing UI fonts rather than bundling desktop fonts.

Hardware acceptance: check empty/one/three books, a deleted recent file, missing
or corrupt cover, unknown progress, long translated titles, all four rotations,
remapped front/side buttons, returning from each action, and repeated Home/book
transitions. Record heap/latency and confirm no radio activation on Home.

The broader native-client scope is tracked in [the parity matrix](docs/ybx-client-parity.md)
and [the statistics adaptation plan](docs/ybx-reading-stats-plan.md). These are
implementation plans; the preview does not yet implement native account statistics
or achievements.

Current implementation checks and open release blockers are recorded in
[the validation status](docs/ybx-validation-status.md).
