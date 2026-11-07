# Firmware integration — 29 October 2026

This consolidates the firmware lanes, including their follow-up fixes. It is a
source integration, not a hardware-accepted release. PocketBook, Desktop and
server branches are outside this task.

| Lane | Source / decision |
| --- | --- |
| Home, account statistics and achievements | Original native-statistics/awards candidate, now included in `806e1bcf`; account totals stay separate from local reader totals. |
| Local reader statistics | `a067f0eb`, reconciled with account panels in `bb32c303`; no replacement of server totals with local values. |
| Session delivery | `59194917`, `485460a6`, `9a7afd34`: recovery, checkpoints and covered-screen timing. |
| YBX position sync | `7cc9bbbd`, `19f7aad8`: use the existing pairing, preserve manual credentials, never forward the YBX key to a foreign server. |
| Catalog tree and unavailable rows | `69063a8e`, `af90892c`: restore selection and disable unavailable actions without affecting unrelated catalogs. |
| On Reader shelf | `e5fa93fb`, `f60d237f`: verified-download marker, confined folder and local open/remove actions. |
| Plugin update notices | `ab0438d7`, `0c9ed0db`, plus integration review fixes for stale notified entries and overflow counts. |
| OPDS deadline/cancellation | `6ef1641c`, reconciled with TLS verification; timeout code must never restore `setInsecure()` or HTTP downgrade. |
| TLS and OTA | `6e77c615`, `efe10d57`, `108a325a`: certificate/hostname checks, board-specific release assets and labeled touch retry. |
| Latest DNS follow-up | `c49fd5c4`, rebased under the TLS patch; dual-stack lookup and bounded busy-slot waits are preserved. |
| Native account sources | Completed the read-only account candidate: show published catalog availability, reuse the plugin token reader and existing pairing owner. Provider login management remains in the cabinet. |
| Upstream sync tooling | `d1e3aa14`, `6f8a0074`, with the inventory extended for the integrated paths. |
| Public screen gallery | `ef08077f`: all ten agreed mockups, separately marked from implemented screens. |

## Preserved work that is not installed twice

The standalone achievements implementation is preserved on
`review/ybx-achievements-20261029` at `7f19d3e2`. Its purpose overlaps the integrated
native awards view. It is not wired in as a second screen/cache/auth consumer.
The earlier native-account skeleton is preserved in its original workspace and
in the integration snapshots; the integrated screen intentionally does not add
another device-code protocol or credential writer.

Snapshots, review inputs/results and build records live in the task artifacts
`artifacts/integration-20261029`. Original agent worktrees have not been reset or
cleaned. The SDK stays pinned to `44427f8`; reproducible patch assets account for
its deliberate local modifications.

## Known limits and acceptance

- Screen mockups are a design reference; build success does not establish pixel
  layout or touch behavior. Calendar and annual-goal UI remain planned.
- The account screen reads the documented plugin manifest fields and accepts only
  the first-party HTTPS lists endpoint. Unsupported manifest representations fail
  closed. Extracting a common versioned native/plugin config adapter remains an
  architecture follow-up, not another authentication implementation.
- Checking up to twelve cached plugin manifests before a Home prompt can add SD
  latency. Measure cold Home entry and repeated returns on a device before release;
  no latency result is inferred from host tests.
- Physical Back/Home is polled during account and OPDS requests, including the
  clock gate. Actual SNTP, DNS, TCP, TLS and display latency need device acceptance.
- The awards API has a bounded list and no cursor; unknown percentages/dates are
  not invented. The On Reader provenance marker does not identify arbitrary files
  copied to the card.
- No device was flashed and no firmware release was published by this integration.
  Use the [hardware matrix](ybx-hardware-acceptance.md) with the exact candidate SHA.

## Final compile checks

Host CTest: **1,356 / 1,356 passed**. SecureNet resumable-fetch and deadline-DNS
host checks passed. Both final firmware targets compiled after the final SDK
correction; compile-check binaries and SHA-256 records are in task artifacts.

| Target | Static RAM | Flash |
| --- | --- | --- |
| `gh_release_rc` | 66888 bytes | 5624901 bytes |
| `x4pro-gh_release_rc` | 111940 bytes | 5476818 bytes |
