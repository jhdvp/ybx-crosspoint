# Native YBX Account — connected sources

## Scope and data flow

The account screen is reachable from **Home → More → My Account** on YBX Home. It reads the already-installed `sd-card/plugins/ybx/device.json` configuration and the token file managed by the existing plugin catalog. No second authentication mechanism is created.

For a configured reader, the screen sends a bounded authenticated `GET https://my.ybx.app/api/v1/crosspoint/catalog/lists` and classifies the returned feed URLs into Yandex Books, WB Books and LitRes. Each recognized source displays its connected/not-connected state only after a valid response. This is a **read-only view of published catalog sources**, not direct OAuth session introspection. Empty or absent feeds are not proof that the user's provider account is disconnected.

Buttons are **Back**, **Refresh** and **Catalogs**. The Catalogs button opens the pre-existing device-code pairing flow; this screen does not mint/revoke tokens, attempt provider-side authentication, nor send credentials anywhere outside the fixed YBX origin.

## Security

- Only the exact first-party `https://my.ybx.app/api/v1/crosspoint/catalog/lists` URL is accepted from the plugin manifest.
- The bearer token is loaded by the existing protected-path plugin token reader, and the manifest Authorization template must match `Bearer {token}`.
- Catalog feed URLs are counted only when they use the exact `https://my.ybx.app/api/v1` origin and a known provider path.
- No access token is printed, persisted, displayed, included in telemetry or logs by this screen.
- Requests have a 12-second timeout and the response is capped at 8 KiB. No automatic periodic polling.
- A rejected token clears the screen's previous source states. A network failure does not claim sources were freshly verified.
- This is *not* a replacement for on-device wolfSSL security review or account management in the web cabinet.

## Tests and release gates

- `ctest -R ^YbxAccountModel\.` verifies source classification, status interpretation, bounded timing and `millis()` rollover.
- Run `pio run -e default` and `pio run -e x4pro` with the same commit.
- Run all host CTests and inspect i18n generation and UI routing.
- Hardware: ensure safe-area layout in X3/X4 portrait screens and X4 Pro touch; verify Back/Refresh/Catalogs, Wi-Fi connection/cancellation, empty list, 401/403 token expiry, 5xx/timeout, reboot; no unexpected token leakage.
- Real provider attachment/removal and tokens remain managed by the existing YBX device-code/cabinet flow. No flash or automatic merge before approval.
