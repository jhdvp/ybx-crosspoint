# YBX Reader — unified firmware integration, 19-source acceptance

Date: 2026-11-04. Branch: integrate/ybx-unified-20261104.
Status: integrated REVIEW candidate, not a release. Never assume all
19 donor repositories or all 17 implementation packages are integrated.

## Actual merge provenance

- Firmware baseline: Stage 0 PR #7, commit a5f842e7.
- Linear descendants: Stats #8/#9, No-SD #11, SD-write #12,
  Native Apps #13, Notes #15, Markdown #16/#17, clipping TLS #18,
  stable Note IDs #19, reanchor #20, local anchors #21, KoSync #22.
  Last source commit: 468649fe.
- Native Plugin 1.9.1 with nested catalogs, separate PR #14,
  branch feat/ybx-native-plugin-191-20261103 at 0d5d30cc.
- Unified merge: b4d69c91. It preserves both merge parents.
- Original pinned SHA/source/license register for all 19 studied
  repositories is reproduced from PR #6 in
  docs/ybx-external-sources-porting-plan.md.
- Server optional context API is separate draft PR #40, PostgreSQL
  migration V22. Production database is V21, compatible via HTTP 404.
- Stock CrossPoint branch ybx/2.0.0rc has a different upstream graph
  and its own nested-catalog #10. Do NOT merge it wholesale onto the
  native candidate. Treat stock and native variants separately.

## 17 packages — verified source scope vs missing acceptance

A package can contain code in this candidate and still be incomplete
against its original acceptance criteria.

| Package | Candidate state | Remaining required evidence or implementation |
| --- | --- | --- |
| P0-A Candidate reconciliation | Combined source, review pending | This build and graph inventory, 38 newer upstream commits/33 overlapping fork paths require a separate conflict review |
| P0-B TLS / network / sleep FIFO | Code present | Real device trusted-time/TLS/DNS/sleep test |
| P1-C Crash-safe SD / boot | Partial: no-SD, Notes, journals | Common transactional writer, power-cut SD hardware tests |
| P1-D Heap / resume / SD writes | Partial: debounce, diagnostic tools | Baseline p95, contiguous heap and battery on C3/S3 |
| P1-E vCodex stats import | NOT implemented | Preview-only importer, idempotency, rollback, cloud reconciliation |
| P1-F EPUB content profiles | NOT implemented in firmware | Server transformations and canonical hash/position migration |
| P1-G QEMU E2E gates | Independent emulator WIP | Repeatable firmware C3/S3 E2E across boot, SD, Wi-Fi and EPD |
| P2-H Native Apps | Stage-1 candidate | Board-gated memory and physical navigation acceptance |
| P2-I Markdown / Notes / quotes | Substantial code candidate | Actual device + server V22 staging, export/conflict UX |
| P2-J Dictionary / footnotes | Upstream features reused | Optional focused UX changes and physical regression |
| P2-K Bilingual EPUB | NOT implemented | Source-aligned pretranslated mode, RTL/CJK tests |
| P2-L Native Library / covers | Partial + Plugin 1.9.1 | Large library performance, physical UI and memory |
| P2-M Saved articles / RSS | NOT implemented | Opt-in server ingestion, credential-safe EPUB/OPDS |
| P2-N Flashcards / study apps | NOT implemented | Optional bounded apps, C3 headroom and lifecycle |
| P2-O Alternate-flash / USB | Research only | Boot partition geometry, fail-safe recovery, physical acceptance |
| P2-P Sleep owner QR/widgets | Partial/design | Consent, bounded offline rendering, power/SD tests |
| P3-Q External board extras | Defer | Explicitly supported target and HAL/RTC evidence |

The 19 public repositories are sources for *selective* ports,
architecture and tests, NOT 19 whole forks to install. GPL/AGPL
donor source and unsupported hardware must not be silently copied.

## Exact combined acceptance required before release

1. SDK gitlink stays 44427f8d51866848b26bea01197f499f9fee5c4b.
   Apply SecureNet patch to local SDK worktree only; never commit
   patched SDK files or change gitlink incidentally.
2. CMake/CTest full suite and plugin installer/catalog tests for
   this merged SHA, not historical PR counts.
3. PlatformIO firmware.bin for default (ESP32-C3 X3/X4) and
   x4pro (ESP32-S3 X4 Pro): binary SHA-256 and flash/RAM.
4. Test native catalog auth, TLS trust policy and no insecure fallback.
5. QEMU acceptance with explicit unsupported peripherals.
6. Physical X3/X4/X4Pro: SD/no-SD, cold boot, page turns, E-Ink,
   touch/rotation, trusted-time TLS, OPDS, notes sync, sleep and OTA.
7. Server V22 must be staged and approved separately; production
   remains V21. No automatic migrations/deployments.
8. Reconcile newer upstream stock commits only in a controlled
   review that protects OTA naming, plugin auth, i18n and SDK pin.

## Next engineering stages after this candidate is reproducible

P1-E: one-time legacy vCodex stats import with dry-run preview.
P1-F: server EPUB profiles with immutable canonical identity.
P1-G: QEMU acceptance gates.
P2-I: actual-device note/quote sync and portable export.
P2-K/M/N/P: optional expansion after RAM/flash acceptance.

A build PASS does not equate to all 17 packages being complete
or production-ready. No automatic main merge, OTA or server deploy.
