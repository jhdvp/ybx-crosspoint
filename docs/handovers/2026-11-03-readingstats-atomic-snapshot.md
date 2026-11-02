# ReadingStats — transactional aggregate and book snapshot (P1-C)

Base: fix/ybx-readingstats-dedup-20261102 (draft PR #8).
Scope: isolated firmware patch, not a production update.

## Defect and design

Previously ReadingStats::recordSession wrote reading-stats.bin before reading-books.bin. A power cut or SD error between operations could permanently desynchronize aggregate and book-level readings. Retrying the event was not sufficient because the ID dedup guard would skip the book update after the first state write.

The new reading-snapshot.bin (YBS1) contains both CPS2 aggregate and book records in one bounded CRC32-validated generation. Writes are staged to reading-snapshot.tmp, verified, and published with rotation through reading-snapshot.bak. Reads prefer valid primary, backup, then temp. Never combine separate generations.

Algorithm independently adapts transactional validation and backup concepts studied in tvhdc/crossvi (lib/Serialization/AtomicFile.cpp and StagedFileTransaction.cpp).

## First-run migration

- If there is no snapshot, read legacy CPS1/CPS2 reading-stats.bin and CPB1 reading-books.bin, including valid legacy temp candidates.
- Import old totals, book rows and the existing recent session ID, then write a snapshot at the next new event. Original legacy files remain untouched as migration-time backups.
- Corrupt legacy files and corrupt snapshots fail closed rather than resetting counters.
- Old firmware will see the unchanged *migration-time* legacy stats after downgrade, not new snapshot records. Backward-write compatibility is NOT promised; require explicit export/migration before downgrade.

## Acceptance

- Pure model tests verify CRC, versions, bounds, old/new generation consistency, backup recovery and fault injection at write/rename/remove steps.
- Firmware ReadingStats.cpp is exercised with a fake SD/HAL adapter: CPS1+book migration, duplicate replay, damaged primary, injected short writes, corrupt legacy refusal and missing card.
- Target firmware builds and full host regression must pass separately.

## Limitations

- This is an application-level FAT recovery protocol. No guarantees against total media failure or arbitrary FAT metadata loss. Real-device power-loss tests remain mandatory.
- Last 16 event IDs are remembered, not unbounded lifetime dedup.
- Up to 32 book records, roughly 4.3 KiB serialized; peak contiguous heap and task stack need real C3 measurement.
- Does not cover PluginQueueStorage, ReaderSessionJournal or unrelated settings; no-SD recovery UI is a separate task.
- Physical hardware acceptance and safe downgrade are release gates; no flash/OTA/deployment in this branch.

## Verified CI-equivalent results (2026-11-03 +03:00)

- Full CMake host test build: PASS.
- Full CTest: **1112/1112 PASS** in 23.54 seconds.
- The ReadingStats-specific suite consists of 35 previously existing tests,
  10 new snapshot serializer/fault tests, and 6 new firmware-HAL migration/fault
  tests. All passed.
- ESP32-C3, PlatformIO env default: **PASS**, 5,678,781 / 6,553,600 bytes
  application flash (86.7%), 66,904 / 327,680 static RAM (20.4%).
  Retained application image: 5,693,312 bytes,
  SHA-256 0481823908643e7cb1d4938f293c318ebaa60ce09879216bc1af52df004690ad.
- ESP32-S3 / X4 Pro, PlatformIO env x4pro: **PASS**,
  5,522,426 / 6,553,600 bytes flash (84.3%),
  111,956 / 327,680 static RAM (34.2%).
  Retained application image: 5,527,696 bytes,
  SHA-256 ef66f67d7ee684556942161f5702d4950c9110524e0f0cc8ec0bca8895dfdb4c.
- Actual images and raw build logs are local to
  ~/Projects/Ybx/artifacts/ybx-stats-atomic-validation-20261103
  and are **not** OTA release assets. The 16 host regression tests use simulated
  failure injection; physical FAT power-loss acceptance is still missing.
