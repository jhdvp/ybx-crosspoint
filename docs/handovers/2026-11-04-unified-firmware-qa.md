# Unified YBX Reader firmware candidate · QA report

Date: 2026-11-04. Candidate source SHA: 0f7785ef.
Branch: integrate/ybx-unified-20261104.
Base Stage 0: a5f842e7. Merged terminal notes/quote stack:
468649fe. Merged independent native plugin 1.9.1:
0d5d30cc. Two-parent merge commit: b4d69c91.

Not production or an OTA, no main merge or physical flash.

## Verified evidence on exact integrated candidate

| Gate | Evidence |
| --- | --- |
| Full CMake/CTest | **1241/1241 PASS** (18.66 seconds) |
| Native plugin installation unit tests | **9/9 PASS** |
| Item downloadability script | PASS |
| Bundled YBX plugin JavaScript syntax | PASS, node --check |
| Source code path inventory | **PASS**, 36 areas, 299 changed fork paths |
| SDK pin | 44427f8d51866848b26bea01197f499f9fee5c4b, unchanged |
| ESP32-C3 X3/X4 PlatformIO default | **PASS**, flash 5,730,231/6,553,600 B (87.4%), static RAM 66,904/327,680 B (20.4%) |
| ESP32-S3 X4 Pro PlatformIO x4pro | **PASS**, flash 5,569,322/6,553,600 B (85.0%), static RAM 111,956/327,680 B (34.2%) |

Saved local firmware image metadata:

- default C3: 5,744,752 bytes; SHA256
  e35b2f1b504ade2b9c305316f29a151bed1e9fc781a5b89ea0c43d273fc06010
- x4pro S3: 5,574,592 bytes; SHA256
  eff3c7f5a2393fcedcccb3f865b9e64adc0d3022cd123a2c161d1ecdb01c3cc3

Images and complete raw logs are retained on the Mac in
~/Projects/Ybx/artifacts/ybx-unified-20261104/.

The FreeInk SDK working tree is modified by the reviewed SecureNet
build patches. This deliberate state appears in git status as
modified freeink-sdk; **the parent repo gitlink is unchanged**.
Do not stage/commit the submodule modified files accidentally.
No proof of bit-for-bit reproducibility across separate clean builds
is implied by these one-run hashes.

## Integration scope and actual gaps

The two-parent merge contains both the full firmware Stage0→PR22
feature stack and native Plugin 1.9.1 PR14. The 19 reviewed external
repositories are documented as source references, not copied wholesale.
See ../ybx-unified-integration-20261104.md for status of all 17
packages. Missing feature packages remain visibly NOT implemented;
a successful build cannot change them to Done.

Server quote context API version 1 depends on a distinct YBX Server
draft PR #40 and optional V22 migration. Production is still on
V21; firmware has an explicit HTTP 404 fallback. No server deploy.

### Upstream merge rehearsal — blocked

Tool: python3 scripts/ybx_upstream_sync.py --no-fetch
(the default is simulation only, not a source merge).

- Target upstream/develop: 308cd522 (38 commits beyond integrated
  upstream base 31dc683f); 157 paths changed by upstream;
  33 overlapped YBX-owned paths.
- **11 conflicted paths**: CrossPointWebServer.cpp, FilesPage.html,
  docs/sd-plugins.md, PluginCatalogActivity.cpp/.h,
  FirmwareIdentity.h, Russian translation YAML,
  docs/webserver-endpoints.md, .github/workflows/release.yml,
  docs/plugin-events.md, PluginEvents.h.
- Hypothetical merged output failed the plugin API name invariant
  for src/network/html/shared/PluginHost.js.inc. This is a blocker
  in that *hypothetical upstream merge*, not a failed test of the
  actual unified source candidate.
- The simulator also verified the existing YBX OTA feed,
  release naming, catalog auth, sleep branding and SD paths.
- No upstream merge was committed; review each hunk to protect
  TLS, PluginHost ABI, account credentials, OTA board naming,
  translations, session/event payloads and files.

Raw merge report:
~/Projects/Ybx/artifacts/ybx-unified-20261104/upstream-simulation/

## Pending acceptance gates

- Actual X3/X4/X4 Pro: cold boot, navigation, C3 largest
  contiguous heap, EPD timing, physical SD failure/power-loss,
  Wi-Fi/DNS/SNTP/TLS trust, safe clipping sync, and OTA rollback.
- Full firmware-level QEMU for the exact combined images
  (different from host unit tests), with missing peripheral
  coverage disclosed rather than counted as PASS.
- Real cross-device sync on a separately authorized staging
  server with V22 and reverse proxy; no live production writes.
- GitHub Actions integration branch matrix and upstream conflict
  reconciliation must finish before promotion.
- Full 17-package acceptance and optional product expansions
  are later stages, not automatically completed by this branch.

Release status: **DRAFT ONLY / NO PRODUCTION DEPLOY / NO OTA**.
