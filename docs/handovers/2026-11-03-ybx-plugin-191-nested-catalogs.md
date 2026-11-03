# YBX Reader 1.9.1 — connected-source navigation

## Baseline

Ported onto `feat/ybx-native-apps-20261103` at `2c2a27b3`, **not**
stock `ybx/2.0.0rc`. YBX Plugin 1.7.0 RC1 is an earlier candidate; the
custom firmware already bundles 1.9.0. Never downgrade YBX-native features
(stats, achievements, account, KoSync, on-reader shelf, OTA and TLS).

## Implementation

- Authenticated dynamic menu entries with `id` and no `url` are groups; `parent`
  selects children. One linked source skips its redundant root directory.
- `browse.fields.folder_url` turns a JSON entry into a navigable directory.
  Folder rows cannot download or claim that a book is on-device.
- Back restores the folder URL/label, then source groups. Cycles and excessive
  group depth are refused.
- A folder carrying a bearer header must match the current catalog's exact
  HTTPS origin; another host, port or http:// is refused *before any request*.
- Firmware advertises `catalog.folders.json`. Plugin **1.9.1** requires this
  and `catalog.lists.auth`. Older firmware with only auth rejects the plugin.
- Source list projection has no extra per-render heap allocation.
- Non-destructive plugin installer still preserves `token.json` and `config.json`.

## Checks, 2026-11-03

- PlatformIO `x4pro` (ESP32-S3): SUCCESS; image generated.
- PlatformIO `default` (ESP32-C3 X3/X4): SUCCESS; image generated.
- CTest selected CatalogFolderNavigation, TLS, CatalogPaging and PluginShelf:
  **36 passed** including 4 new hierarchical-menu tests.
- Python installer tests: **7 passed**; Node plugin syntax: PASS.
- Official pinned QEMU 11.1.2, disposable isolated guest built from this
  X4 Pro firmware: boots, mounts test SD, draws Home, More, and File Transfer
  network selector with real guest UART and EPD frames.
- Guest HTTP `/api/plugins` not confirmed: File Transfer web server starts only
  after network selection, not merely entering its activity. QEMU virtual time
  also reached auto-sleep before later manual input.
- Real account pairing, provider EPUB download and physical hardware:
  **NOT YET VALIDATED**.

Evidence is local only:
`~/Projects/YBX/artifacts/ybx-native191-qemu/`. No production credentials
were used or copied to this branch.

## Rollout gates

1. Integrate into the active custom-firmware branch without resetting other
   agent's uncommitted changes; do not merge the stock RC binary.
2. Test network-enabled File Transfer and Plugin Hub in isolated QEMU;
   device-code flow should obtain the guest's own token through normal approval.
3. Check one/multiple/zero sources, `Все`, `Все книги`, reading states,
   series and shelves, EPUB delivery, backward navigation and duplicate folding.
4. Test upgrades, SD preservation, OTA rollback and hardware separately.
5. Issue #9 (pairing/QEMU) and #10 (legacy insecure TLS) remain open until
   corresponding end-to-end and legacy compatibility checks complete.
