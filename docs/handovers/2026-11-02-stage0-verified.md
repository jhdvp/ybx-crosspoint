# YBX Reader Stage-0 integration: verified build/test evidence

**Date:** 2026-11-02 (+03:00).
**Branch:** `integrate/ybx-stage0-20261102`, based on firmware integration candidate `979386ee` (29 October 2026).
**Production/OTA:** NOT DEPLOYED. No firmware image was flashed onto hardware.

## Merged in this isolated candidate
- Carried across newer bundled YBX plugin behaviour: 1.6.4→1.6.6 source updates and YBX icon; the manifest stays at firmware-bundled plugin **1.9.0** to preserve native statistics/awards descriptors.
- Saved and linked Rate Cloud trial handover (three documentation commits). Individual agent WIP fixes remain **unmerged**.
- Extended `docs/ybx-fork-inventory.md` for `docs/handovers/**`, so `python3 scripts/ybx_upstream_sync.py --check-inventory` passes.
- FreeInk SDK is pinned to `44427f8d51866848b26bea01197f499f9fee5c4b`; build scripts apply the reviewed network patch series in the **submodule working tree**. These expected uncommitted SDK patch files are **not** a gitlink change and must not be committed accidentally.

## Device-target compilation

| Board target | Result | firmware.bin bytes | SHA-256 |
|---|---|---:|---|
| `default` | PASS | 5690592 | `1545ce4e0f540ef75a5ee747c45eca9cf9682f59c28f819cc3187c70c92f9ee0` |
| `x4pro` | PASS | 5525776 | `7e0efd3dd8578d72d93b4c53cd2c86d0455aa6d4f4d2eb3bf1185e98c74d4ef1` |


Measured build summaries:
- `default`: RAM:   [==        ]  20.4% (used 66904 bytes from 327680 bytes); Flash: [========= ]  86.6% (used 5676063 bytes from 6553600 bytes)
- `x4pro`: RAM:   [===       ]  34.2% (used 111956 bytes from 327680 bytes); Flash: [========  ]  84.2% (used 5520518 bytes from 6553600 bytes)


## Host and install validation
- CMake host build retry (-j2): PASS.
- GoogleTest/CTest: **1091/1091 passed**, 25.79 seconds, no failed cases.
- `node --check sd-card/plugins/ybx/plugin.js`: PASS.
- `python3 -m unittest discover -s test -p test_ybx_plugin_install.py -v`: **6/6 passed**.
- `python3 scripts/ybx_upstream_sync.py --check-inventory`: PASS (26 inventory areas).
- Initial host build with -j6 had GoogleTest-discovery timeouts at 5 seconds under concurrent load; retry with -j2 compiled and registered all test executables, then all tests passed. This is *not* a runtime failure of an assertion.
- Local raw logs and firmware images are in `~/Projects/Ybx/artifacts/ybx-stage0-validation-20261102/`; do not attach private environment logs to a public PR without sanitizing.

## Release blockers
- Hardware X3/X4/X4 Pro cold boot, TLS handshake, OTA and rollback, and SD failure/power-loss acceptance have **not** been tested on physical devices.
- Current upstream `develop` is 32 commits ahead of the fork-integrated upstream base; 29 changed paths overlap fork-owned paths. No automatic upstream merge was attempted.
- One-live-session dedup in `src/util/ReadingStats` was flagged by Rate Cloud #45 / rc-fw-06. Its agent branch contains *characterization tests that expect broken behaviour*, not an accepted fix. Treat as a separate test-driven bugfix PR.
- Local Kaneo HTTP endpoint at `127.0.0.1:5173` did not respond during this validation; do not claim board status changes.
- Draft PR only: do not merge or make release assets until device gates and review are satisfied.
