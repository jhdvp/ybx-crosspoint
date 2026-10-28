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
