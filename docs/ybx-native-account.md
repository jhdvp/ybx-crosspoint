# Native YBX Account — connected sources and native pairing

## Scope and data flow

The account screen is reachable from **Home → More → My Account** on YBX Home. It reads the already-installed `sd-card/plugins/ybx/device.json` configuration and the token file managed by the existing plugin catalog. No second authentication mechanism is created.

For a configured reader, the screen sends a bounded authenticated `GET https://my.ybx.app/api/v1/crosspoint/catalog/lists` and classifies the returned feed URLs into Yandex Books, WB Books and LitRes. Each recognized source displays its connected/not-connected state only after a valid response. This is a **read-only view of published catalog sources**, not direct OAuth session introspection. Empty or absent feeds are not proof that the user's provider account is disconnected.

When a reader token is missing or rejected, **Sign in** starts the existing server's OAuth device-code protocol natively: `POST /auth/device`, an HTTPS-only QR/typed user code, and bounded scheduled `POST /auth/token` polling with pending, slow-down, expired, denied, cancelled and transient transport states. The account screen saves a confirmed token to the **same** protected plugin token file that the catalog uses. No login passwords or provider credentials are entered on-device.

Buttons are **Back**, **Refresh/Sign in** and **Options** (Manage sources, Switch account, Catalogs, Cancel). During pairing the actions change to **Cancel** and **Show QR**; portrait layouts may additionally embed a compact QR, and the full-screen QR remains available for smaller screen orientations. Returning from the QR resumes polling. The Catalogs button retains the original plugin experience, not a parallel token store.

## Security

- Only the exact first-party `https://my.ybx.app/api/v1/crosspoint/catalog/lists` URL is accepted from the plugin manifest.
- The bearer token is loaded by the existing protected-path plugin token reader, and the manifest Authorization template must match `Bearer {token}`.
- Catalog feed URLs are counted only when they use the exact `https://my.ybx.app/api/v1` origin and a known provider path.
- Device-code secrets and access tokens are never displayed, attached to QR, or written to logs. The access token is persisted **only** by the existing protected plugin token writer after approval.
- Catalog requests have a 12-second timeout and 8 KiB cap; device-code responses use the same timeout and 3 KiB cap. Polls are bounded to the server interval (minimum 3 s) with slow-down backoff (maximum 60 s), and the pairing lifetime is bounded to 60–3600 s. No polling continues after cancel or expiry.
- A rejected token or network failure clears the screen's previous source states; neither is presented as newly verified.
- On successful re-pair, the previous account's queued events and token-scoped statistics/achievement caches must be cleared before a new token can be saved, or the operation fails closed. This intentionally avoids carrying account-scoped data across users; it does not delete local books or positions.
- Automatic replacement of a **currently valid** account is blocked. The explicit Switch account menu requires confirmation. Provider unlink continues to require authentication in the web cabinet.
- This is *not* a replacement for on-device wolfSSL security review or account management in the web cabinet.

## Tests and release gates

- `ctest -R ^YbxAccountModel\.` verifies source classification, first-party QR challenge, token shape, backoff, expiry bounding and `millis()` rollover.
- Run `pio run -e default` and `pio run -e x4pro` with the same commit.
- Run all host CTests and inspect i18n generation and UI routing.
- Hardware: ensure safe-area layout in X3/X4 portrait screens and X4 Pro touch; verify Back/Refresh/Options/Catalogs, management QR, switch confirmation and cancellation, Wi-Fi connection/cancellation, empty list, 401/403 token expiry, 5xx/timeout, reboot; no unexpected token leakage.
- Real provider attachment/removal still occurs in the existing YBX web cabinet, not on the reader. The account-switch confirmation is implemented; proactive per-provider session-health checks and native provider unlink are **not implemented**. No flash or automatic merge before approval.

## Account management and source repair (29 October 2026)

- **Manage sources** displays a fixed, credential-free QR to `https://my.ybx.app/cabinet#sources`. Connector link/repair/logout endpoints require an authenticated cabinet session; the device bearer is deliberately not elevated to that privilege. Per-provider unlink is not performed directly from the reader.
- The account menu offers **Manage sources**, **Switch YBX account** (only for a stored token), **Catalogs**, and **Cancel**.
- Switching requires an explicit confirmation. The old token remains valid while the new device-code pairing is pending or cancelled; books and local reading positions are not deleted.
- After a new token is approved, an allowlist clears only `events.jsonl`, token-scoped reading-summary/achievement cache files and their bounded pending/backup/response variants. A cleanup failure refuses the switch, avoiding cross-account event delivery. The new token uses staged SD write, recoverable backup and rename promotion; shared token readers and writers are serialized. Interrupted promotion recovers the old token, never an uncommitted token.
- Account/source errors are distinguished by exact semantics: 401/403 invalid reader token, the `X-YBX-Device-Removed` header indicating a removed device, and the explicit `409 detail.code=books_consent_required` indicating provider authorization that needs repair. Unknown 409 is **not** interpreted as consent failure.
- **API limitation:** the current `/catalog/lists` endpoint enumerates linked providers; it does not check each upstream session. It normally does not generate provider-session 409 itself. A linked source is therefore *not proof of a working provider login*. A future server-side read-only source-health endpoint would be needed for proactive per-provider health diagnosis.

Validation gates include host tests for recognition of device removal, known/unknown 409, strict scoped cleanup, and atomic token recovery across staged-write/promotion failures. On-device source QR, account-switch cancellation, SD power-loss recovery, concurrent event delivery, provider cabinet login and all hardware orientations remain unverified until physical acceptance.
