# YBX Reader — hardware acceptance matrix

This checklist is for the candidate `integrate/ybx-settings-candidate-20261029` and must be updated with the exact tested SHA. **A successful host/firmware build is not an on-device pass.**

## Verified in the development environment (29 October 2026)

| Gate | X3 / X4 (ESP32-C3) | X4 Pro (ESP32-S3) |
|---|---|---|
| PlatformIO firmware image | PASS (`default`) | PASS (`x4pro`) |
| Static RAM | 66,792 / 327,680 bytes (20.4%) | 111,844 / 327,680 bytes (34.1%) |
| Firmware partition | 5,652,809 / 6,553,600 bytes (86.3%) | 5,498,410 / 6,553,600 bytes (83.9%) |
| C++ host regressions (shared) | 1,052/1,052 PASS | 1,052/1,052 PASS |
| Device boot / UI | NOT TESTED | NOT TESTED |
| Trust-checked wolfSSL handshake on device | NOT TESTED | NOT TESTED |
| OTA installation & recovery | NOT TESTED | NOT TESTED |

Host checks: TLS policy, bounded DNS/TCP, OTA release policy, local reading session and statistics. The current five-CA bundle validated the names and certificate chains of six service endpoints with host OpenSSL, not with the reader's wolfSSL build.

## Setup and safety prerequisites

- Record the tested SHA, firmware build ID, device model, firmware version, available OTA slot capacity, clock state, battery level and operator.
- Capture current working firmware/recovery procedure before any flash. Do not flash an unconfirmed hardware variant or overwrite unique device data.
- Use a disposable test account and books; never put account tokens, pairing codes, private keys, personal reading histories, or real credentials into issue attachments or logs.
- Keep serial output and network captures private; redact Authorization headers, device token, cookies, MAC/serial and identifying book titles.
- Run one model at a time, with an explicit stop/recovery path and safe power source.

## Acceptance cases — repeat for each physical model

| ID | Test | Acceptance criterion | X3 | X4 | X4 Pro |
|---|---|---|---|---|---|
| HW-01 | Cold boot and first render | No reboot loop, correct display orientation, responsive input | ☐ | ☐ | ☐ |
| HW-02 | YBX Home, Library, Statistics and Awards | Can navigate and go back; no out-of-range focus or missing translations | ☐ | ☐ | ☐ |
| HW-03 | Buttons and touch controls | All physical keys work; X4 Pro taps only trigger labeled actions, no accidental OTA | ☐ | ☐ | ☐ |
| HW-04 | Offline mode with cached shelf/statistics | Usable offline, no repeated retry loops or missing local content | ☐ | ☐ | ☐ |
| HW-05 | Valid TLS: YBX and release host | Secure connection succeeds with correct time and hostname | ☐ | ☐ | ☐ |
| HW-06 | Invalid hostname / unknown CA | Connection fails closed, including after HTTPS redirects | ☐ | ☐ | ☐ |
| HW-07 | Clock unset / SNTP unavailable | Bounded wait, safe failure message, no insecure fallback | ☐ | ☐ | ☐ |
| HW-08 | DNS unreachable / captive portal | Bounded timeout without wedging UI or sleep transition | ☐ | ☐ | ☐ |
| HW-09 | Auth redirect to another origin | Authorization header never forwarded to unrelated origin | ☐ | ☐ | ☐ |
| HW-10 | Reader session, exit and sleep | Exactly one session delivered, correct duration and final progress | ☐ | ☐ | ☐ |
| HW-11 | Crash/restart/offline backlog | Durable queue, no double-count or dropped delivered session | ☐ | ☐ | ☐ |
| HW-12 | OTA no update / missing board asset | Informational state, no flash attempt | ☐ | ☐ | ☐ |
| HW-13 | OTA untrusted certificate / wrong chip / corrupt size | Refused safely before or during download, no boot slot corruption | ☐ | ☐ | ☐ |
| HW-14 | OTA retry after interrupted download | Explicit retry, correct progress, previous stable slot recoverable | ☐ | ☐ | ☐ |
| HW-15 | OTA successful update / rollback | Correct version after reboot and verified return to prior firmware | ☐ | ☐ | ☐ |
| HW-16 | Repeated navigation / memory pressure | 20-minute test, no crashes, stuck input or runaway heap loss | ☐ | ☐ | ☐ |

**Release gate:** all mandatory tests pass on every advertised hardware variant, with owner sign-off and recovery artifacts. Passing builds and host tests alone does **not** authorize uploading a release or setting a Kaneo card to done.

## Evidence record

- Device / model:
- Tested Git SHA:
- SDK pinned SHA and patch hashes:
- Firmware version and build mode:
- Tester and date:
- Test IDs passed/failed/skipped and reasons:
- Reproduction steps, sanitized serial log and SHA-256 of image:
- Recovery / rollback verification:
- Owner approval:
