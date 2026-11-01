# YBX implementation status — 28 October 2026

This branch saves ongoing implementation work. Firmware handoff is incomplete;
these checks do not establish release readiness or hardware support acceptance.
The maintainer explicitly requested committing and pushing the current state
with the outstanding limitations already reported. This does not record design
acceptance or waive the remaining release checks.

## Implemented

- Compact X3/X4 Home using the existing UI host, local recent books, saved
  progress and a Library / Catalogs / More row. No account statistics are invented.
- Opt-in latest sleep GET delivery reserves an attempt after ordinary FIFO
  events, even when an earlier event fails. Older sleep requests are removed
  only after the latest succeeds.
- Queue replacement uses a complete pending file and a recoverable backup.
  Append repairs a bounded incomplete trailing event before writing another.
- Sleep drains use a cooperative time budget; authentication and retry share
  the event deadline. Streamed downloads require successful file close.

## Verification

- Host suite: **897/897 passed**, including 22 queue, storage and deadline cases.
  Storage tests cover short writes, failed renames/rollback, interrupted
  transaction boundaries and all 511 partial-append positions.
- Formatting and `git diff --check`: passed.
- Final `gh_release_rc` build: passed; static RAM 66,696/327,680 bytes,
  flash 5,493,963/6,553,600 bytes.
- Final `x4pro-gh_release_rc` build: passed; static RAM 111,756/327,680 bytes,
  flash 5,350,302/6,553,600 bytes.
- FreeInk SDK remains pinned at
  `44427f8d51866848b26bea01197f499f9fee5c4b`, without local SDK changes.

Static build sizes are not runtime heap measurements. Host storage models do
not establish physical FAT/SD durability. Build logs, source patches and binary
checksums were saved locally in `artifacts/ybx-sleep-recovery-20261028` in the
parent YBX workspace; generated binaries and logs are not committed here.

## Outstanding

- Four independent final reviews are incomplete. OpenCode Go correctness,
  architecture and embedded attempts timed out without substantive results;
  Qoder documentation review failed on its credit limit. An earlier Home
  architecture review does not cover all final changes.
- Synchronous SDK DNS/TCP and SD operations can exceed the cooperative sleep
  budget. No hard upper bound on sleep latency is claimed.
- The inherited 4 KB outbox overflow policy still drops the queue wholesale;
  shared attempt limits and subscription order can starve later plugins.
- Native account statistics and achievements are planned, not implemented.
- X4 Pro compiles, but still uses the upstream Home. YBX touch adaptation,
  USB branding and Pro release/OTA assets remain unfinished.
- Inherited HTTPS certificate verification remains disabled. Transport/OTA
  trust hardening is a separate release blocker.
- No device flashing, hardware validation, release or PR is part of these checks.

See [the fork notes](../YBX_FORK.md), [client parity](ybx-client-parity.md),
[statistics plan](ybx-reading-stats-plan.md) and
[event delivery contract](plugin-events.md) for scope and acceptance steps.

## Native YBX account screen (resume, 2026-11-01)

Adds the on-device YBX account flow: `src/activities/YbxAccountActivity.{h,cpp}`
(device-code sign-in with QR, account status, connected sources, cabinet QR,
sign-out), pure logic in `src/util/YbxAccountModel.h`, entry via the Home
"More" popup (`src/activities/home/HomeActivity.*`, `src/util/YbxHomeLayout.h`),
`ActivityManager::goToYbxAccount()`, and 21 STR_YBX_ACCOUNT_* strings
(English + Russian; other languages fall back to English). The screen reuses
five existing `STR_PLUGIN_*` pairing strings, which now also have Russian
values. Firmware-handoff reviews (correctness, architecture, embedded,
i18n) were run; accepted fixes: 48 KB response cap parity with the plugin
catalog lists endpoint, `auth.type` default aligned with the catalog,
row publish under `RenderLock`, token-save failure message, tested
deadline helper in the input path, Russian wording for the pairing
screens.

Completed checks (host only):

- `pio run -e default`: success; RAM 66,712/327,680 B (20.4%),
  flash 5,562,093/6,553,600 B (84.9%); no new warnings from these files.
- `test/ybx_account_model` gtest suite (9 tests): list-URL classification,
  HTTP status → outcome mapping incl. device-removed, pairing timer clamps,
  wrap-safe deadline comparison, poll error codes, connected-source summary,
  cabinet URL derivation and buffer bounds.
- `test/ybx_home` updated for the fourth More-popup choice (account
  sentinel destination); both suites pass.
- `bin/clang-format-fix -g` could not run: no clang-format ≥ 21 on this
  host. Formatting must be re-checked by the owner before merging.

Owner acceptance on device (not performed by these checks):

- [ ] Home → More → "YBX Account": screen opens in all four orientations,
  buttons and touch both navigate, hints map correctly.
- [ ] First sign-in: QR and user code appear; pairing on the phone completes
  and the status changes to signed-in; leaving the screen mid-pairing cancels
  the poll without leaking the TLS session.
- [ ] Expired code and denied pairing show the failure and allow retry.
- [ ] Existing token: refresh shows the true connected sources only
  (connect/disconnect each provider in the cabinet and confirm the list
  changes); shelf counts match the cabinet.
- [ ] Revoked key (log out in cabinet): status offers repair/sign-in, not
  "offline"; unlinking the device shows the device-removed message.
- [ ] Provider session broken on the phone side: distinct provider-consent
  message, sources row hidden, cabinet QR still shown.
- [ ] Airplane mode: status row reports offline, cabinet QR still renders
  from the manifest, no crash on retry.
- [ ] Sign-out: token file and plugin outbox cleared, local books and reading
  positions unchanged, sleep screen cache regenerated.
- [ ] The account screen works on a device where the YBX plugin web page has
  never been opened (manifest + token files only).
- [ ] Heap after repeated open/close and pairing attempts: >50 KB free,
  no growth across cycles (C3 baseline).
