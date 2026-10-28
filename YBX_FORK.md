# YBX Reader firmware

This is the YBX firmware fork of CrossPoint Reader for Xteink X3/X4.
The web panel retains the upstream English interface. Reader screens use the
selected device language; the language-invariant YBX brand has English fallback.

The initial integration is based on upstream `develop`. The last integrated
upstream commit is recorded in [the sync log](docs/ybx-sync-log.md); the full
upstream history and MIT license remain intact.

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

## Syncing upstream CrossPoint

`scripts/ybx_upstream_sync.py` is the reproducible procedure. It fetches
`upstream/develop`, compares the ranges, simulates or starts the merge, groups
every conflict by YBX area, checks the protected YBX behaviour and runs the build
and host tests. It never pushes, never forces and never resets fork work. Reports
and build logs go to the ignored `.ybx-sync.local/` directory. Exit status:
`0` nothing to integrate and nothing flagged, `2` open conflicts or failed checks
(a human must decide), `1` usage or git error.

1. Simulate first. This changes no tracked file, moves no branch and needs no
   clean tree; it only writes the report:

   ```sh
   python3 scripts/ybx_upstream_sync.py
   ```

   Read **Both-side churn** (paths upstream and the fork both changed, grouped by
   area with risk and handling) and **Conflicts by YBX area** (each conflicted
   region with the reason the fork owns it).

2. Validate the inventory. It fails when a changed path is missing from
   [the inventory](docs/ybx-fork-inventory.md) or when an area no longer matches
   any file. Add the missing row with its reason before merging:

   ```sh
   python3 scripts/ybx_upstream_sync.py --check-inventory
   ```

3. Start the real merge on a throwaway sync branch. The script creates
   `sync/upstream-<sha>-<date>` from the current branch and merges with
   `--no-ff --no-commit`, so nothing is committed and the fork branch stays as it is:

   ```sh
   python3 scripts/ybx_upstream_sync.py --merge
   ```

   Resolve each conflict by area, not per hunk. `reapply` means take the upstream
   side and re-apply the YBX rename. `merge` means judge the hunk against YBX
   behaviour. `regenerate` means take one side and let the generated file be rebuilt:
   `gen_i18n.py` and `build_html.py` run as build `pre:` hooks
   (`lib/I18n/I18nStrings.*`, `I18nKeys.h` and `src/network/html/*.generated.h` are
   ignored output, never merge them); `src/images/Logo120.h` needs
   `python3 scripts/generate_ybx_logo.py` after the PNG side is resolved.

4. Build and test on the sync branch, then record the run. Re-running with
   `--merge --build` while the merge is in progress reports the remaining
   conflicts and the invariant state of the working tree:

   ```sh
   python3 scripts/ybx_upstream_sync.py --no-fetch --merge --build
   ```

   All YBX invariants must be `OK` and the build and host tests must pass before
   you commit. Commit the merge yourself once you have read every resolved area;
   the commit message records the integrated upstream SHA.

5. Confirm and log the sync after the merge commit:

   ```sh
   python3 scripts/ybx_upstream_sync.py --record docs/ybx-sync-log.md
   ```

   It reports `upstream/develop` as already integrated, prints the new merge base
   as the integrated upstream SHA and appends one entry to the sync log.
   [docs/ybx-sync-log.md](docs/ybx-sync-log.md) is the single place that records
   the integrated SHA and open conflicts.

6. Back out at any point. This aborts the in-progress merge, returns to the branch
   you started from and deletes the throwaway sync branch:

   ```sh
   python3 scripts/ybx_upstream_sync.py --abort --sync-branch <name> --return-to <branch> --delete-sync-branch
   ```

Two areas need a deliberate decision rather than a mechanical merge. The release
feed (`src/network/OtaUpdater.cpp`, `.github/workflows/release*.yml`) must keep the
fork repository, the `ybx-` asset prefix and the single `x3-x4` matrix row: an
auto-merged upstream line can silently point YBX devices at an upstream image.
The OTA version comparison and the plugin queue contracts are function rewrites,
so compare the function bodies against upstream instead of trusting a clean merge.

There is no automatic installation of upstream binaries. The script only integrates
source; the device checks stay manual and are listed in
[the validation status](docs/ybx-validation-status.md). An upstream change to the
pinned FreeInk SDK submodule is reported as an SDK pin line: advance it in its own
commit, not inside the sync merge.

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
