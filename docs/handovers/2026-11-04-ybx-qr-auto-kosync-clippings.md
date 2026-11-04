# YBX QR pairing → automatic KOSync + Clippings v2

**Status: candidate for custom YBX firmware, NOT a Stock 2.0 RC patch.**

`src/util/YbxKosync.cpp` already reconciles the built-in KOSync password,
username and server URL from `plugins/ybx/token.json` on QR pairing and
subsequent launches. Do not implement a second independent credential manager.
It preserves a manually configured third-party KOSync server; this change
also covers the case where a manual URL has been entered without a password.

New behavior: a genuinely fresh pairing enables `Sync Clippings` along with
KOSync. Re-pairing, automatic credential refresh or changing source later
never turns a manually disabled clipping toggle back on.

The firmware's `ClippingSync` now adds `Authorization: Bearer cp_…` **only**
for the exact URL `https://my.ybx.app/api/v1/crosspoint/kosync` and username
`ybx`, with an exact-format paired token. The existing verified TLS transport
policy is mandatory. Other user-configured servers retain `x-auth-user` and
`x-auth-key` and never receive the YBX bearer token.

Paired readers require the matching server route and newly minted `clippings`
scope. Legacy `cp_` keys must be re-paired; progress-only `kosync_` keys cannot
read or write clippings. Server asynchronously carries quotes to Yandex and
pulls provider quotes back. An accepted upload alone does not demonstrate a
completed Yandex write.

Host validation: `ctest -R '^YbxKosyncDecision\\.'`, including opt-in,
manual URL preservation, token boundary and TLS destination cases.
Build verification: `python3 -m platformio run -e x4pro`.

**Release gates:** pair a fresh synthetic test device in QEMU and verify
`koreader.json` without web UI; prove one real guest clipping GET/PUT/GET;
then use one mapped Yandex EPUB and outbox/Yandex readback without writing to
an unrelated user's books. Diagnose the earlier official Stock 2.0 RC
`CORRUPT HEAP` crash separately before claiming a stable firmware E2E.

The unmodified upstream Stock 2.0 RC cannot autoconfigure on QR and still
needs the web panel; do not describe this fork feature as already shipped to
Stock users. Production YBX and OTA were not changed by this development.
