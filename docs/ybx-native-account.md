# YBX Reader — built-in first-party account and catalog

## Ownership

The fork now owns YBX device-code pairing, reader key persistence, connected source
status, reading summary/achievements, and the built-in catalog entry point.
**None requires an installed SD plugin or the global Plugins switch.** The old
YBX Native 1.9.1 SD plugin is still supported for stock CrossPoint migration.

- On-device: **Home → More → My Account → Sign in**. When Wi-Fi is already
  connected and the reader is unpaired, the QR request starts on entry.
- QR and user code come exclusively from
  `POST https://my.ybx.app/api/v1/crosspoint/auth/device` with
  `{"client_id":"crosspoint"}`.
- Reader polls `POST .../crosspoint/auth/token` using the OAuth device-code
  grant and strictly bounded interval/expiry. Pending, slow-down, denial,
  expiry, malformed payload and TLS/transport errors have distinct handling.
- TLS must validate the YBX certificate chain. The current Let's Encrypt
  YE1 leaf certificate uses ECDSA/SHA-384; the fork's wolfSSL build explicitly
  compiles SHA-384/SHA-512 support to avoid HASH_TYPE_E (-232).
  Do not disable host/certificate verification for emulator convenience.
- The QR URL is allowed only on the fixed YBX HTTPS origins. Never log the
  device code, bearer token, QR URL with code, or the user's provider tokens.
- A successful grant is atomically written to
  `/.crosspoint/ybx-account.json` with backup/rollback protection.
  ProtectedPaths excludes this file and its temp/backup siblings from
  WebDAV and other untrusted browser/plugin access, including FAT aliases.
- Already-paired SD plugin tokens are read as a legacy fallback without
  deleting them. A new native grant can mirror to an installed plugin; if the
  mirror fails the built-in account still works.
- Native KOSync reads the same reader key. It respects the existing manual
  server override/opt-in rules; no forced server overwrite.
- **My Account → Sync** opens the built-in KOReader synchronization settings.
  YBX KOSync, Send Metadata, and Sync Clippings remain independently configurable:
  key rotation or server re-provisioning must never silently enable metadata or
  clippings. Clippings are opt-in and use verified HTTPS.
- Native statistics/achievements use pinned YBX API routes and a separate
  reader-owned cache under `/.crosspoint/ybx/`, keyed by a SHA-256 binding
  to the reader token. A matching legacy plugin cache is read as fallback.

## Native YBX Books

**My Account → Catalogs** enters the built-in YBX catalog, without scanning
`/plugins` or reading a device manifest from SD. The CrossPoint catalog
renderer/network/download engine is reused to avoid a second EPUB navigation
pipeline, but the YBX service contract is compiled into firmware. The
selected shelves, Yandex/WB/LitRes listings, folder navigation, searches,
downloads and sidecars are handled by the same authenticated first-party
YBX endpoints as the compatibility plugin.

The bundled catalog's manifest is a *compiled-in C++ constant*, not an SD
plugin. Its compatibility contract is checked against the separately
installable 1.9.1 plugin at source time. The native identity still belongs
to the firmware. The compatibility plugin remains optional for stock
CrossPoint and any legacy plugin-specific events.

## QEMU 11.1.2 native sign-in evidence (2026-11-04)

The official GitHub Actions firmware artifact from [CI run #75](https://github.com/jhdvp/ybx-crosspoint/actions/runs/41826248682)
on commit `551f530b22bddebcca3a051bcbf89280ac77b694` has SHA-256
`03bbb39c3a5986d01ef331854dcc30a7f461f822f89eec25fc90553d333ab05a`
(`firmware.bin`, X4 Pro). It is **not** the locally built image.

- Pinned QEMU 11.1.2 / eink-qemu 0.9.2: release SHA verification passed.
- Constructed a disposable 16 MiB Flash image from the CI application and
  verified factory bootloader/partition files; kept production emulator,
  stable reader state, and OTA untouched.
- Used a separately constructed FAT16 SD test image containing only a synthetic
  `PICSimLabWifi` open-network credential. The SD contains **no YBX plugin**,
  account token, provider credential or real user book.
- In guest: Home → More → My Account → Sign in. The guest joined Wi-Fi
  via the saved open SSID, completed real DHCP and an HTTPS TLS **1.3**
  handshake with `TLS_AES_128_GCM_SHA256` (certificate verification enabled).
  The earlier wolfSSL `HASH_TYPE_E (-232)` no longer occurs.
- The first-party device-code POST returned real challenge data; the genuine
  E-Ink screen displayed a QR, an alphanumeric pairing code, and a URL under
  `https://my.ybx.app/pair`. Do not attach live QR/screenshots or expired
  secret codes to public PRs; they are temporary authentication material.
- Native screen and QR are confirmed **without optional SD plugin**. Full
  user-approved pairing, subsequent token persistence, authenticated catalogs,
  and bidirectional KOSync/metadata/clipping exchange remain open acceptance
  items; a pending grant is not a successful account login.
- Native CMake/CTest: 1245/1246 in parallel; the lone unrelated
  `FlowBoundaries/ParserStopTest.PageCallbackStopsParsingAndClosesFile`
  succeeded when its four cases were rerun in isolation. GitHub CI unit-tests
  and all firmware target builds passed, while cppcheck has existing
  low-style warnings also present on the parent PR.
- QEMU Wi-Fi list touch has a separate input-routing issue: a row visually
  selects but does not always activate. The test-only preseeded open-network
  fixture bypassed this GUI problem; it is **not** a production workaround.

## Test status and limits

- Portable policy + ProtectedPaths and full CTest suite; Python
  `test/test_ybx_builtin_catalog_manifest.py`.
- ESP32-C3 and ESP32-S3 builds required from the same head commit.
- Emulator: QEMU 11.1.2 confirmed the My Account / Sign in / Catalogs UI
  without an SD YBX plugin. Initial QR HTTPS request hit wolfSSL -232; the
  SHA-384 rebuild passed CI, but **live QR and end-to-end pairing still need
  a new QEMU and/or hardware test**. Do not claim user authorization was
  granted or that clipping bidirectional sync was exercised.
- Never publish QR codes, live tokens, secrets, or account-specific
  reading histories in logs/issues/commits.
- Real X4 Pro hardware is required for power-button wake, long sessions,
  E-Ink grayscale, TLS trust and persistence after true deep sleep.
- **Not finished**: fully native provider linking/unlinking and offline
  event delivery without the optional plugin's script. Provider OAuth
  must remain server/phone-based; the reader must not store provider secrets.
