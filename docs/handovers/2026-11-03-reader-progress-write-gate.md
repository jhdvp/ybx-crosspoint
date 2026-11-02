# YBX Reader · bounded reader progress SD writes (Kaneo #54)

## Provenance and scope

Informed by the MIT-licensed
[Sichroteph/YACP ReaderProgressSaveDebouncer.h](https://github.com/Sichroteph/YACP/blob/23568ad35807/src/activities/reader/ReaderProgressSaveDebouncer.h),
commit 23568ad35807; YBX implements an independent, host-testable
ProgressWriteGate compatible with its current EPUB and XTC readers, not a
whole-fork merge.

Stacked integration baseline: fix/ybx-nosd-recovery-20261103, following
the current staged YBX review PRs. No production or release deployment.

## Existing behavior and change

- EpubReaderActivity::renderBook() used to rewrite the atomic progress
  file for every changed page, potentially generating two or more FAT
  directory operations per page turn.
- XtcReaderActivity::renderBook() previously rewrote progress for every
  repaint, even when the saved page had not changed.
- EPUB's first rendered page remains immediately durable; subsequent changed
  positions are coalesced, normally persisting after 8 changes or 60 seconds
  since the last successful save, whichever is reached first.
- EPUB's main loop checks for a due idle write under the existing RenderLock;
  it does not add an SD timer task or network traffic.
- EPUB flushes any unsaved progress before exit, before sleep, and before
  another activity covers the reader. Existing direct KoSync progress saves,
  reader.session journal, reading-statistics transactions and event queues are
  unchanged.
- Failed saves keep pending state, throttle automatic retry for 2 seconds and
  log the error. An explicit lifecycle flush still attempts the last position.
- XTC skips same-page redrawing writes only after a successful previous save.
  It retries a failed save on the next render; changed pages still save
  immediately.
- Policy is allocation-free, wraparound-safe and independently host-tested.

## Trade-off and remaining gates

- The power-loss recovery point may lag the current displayed EPUB page by
  up to 7 intervening turns or 60 seconds during steady operation. An
  unclean power cut is not an explicit lifecycle flush.
- No claim about physical SD throughput or battery improvement without
  repeatable before/after hardware measurements.
- Test missing/card-full/corrupt SD, fast page-turning, suspend while
  on a child UI, remote sync and power-loss tests on real X3/X4/X4 Pro.
- The canonical progress.bin writer still uses a temporary file followed
  by a non-metadata-atomic remove+rename on FAT. This change does not claim
  to repair that unrelated filesystem failure window; a separate durable
  backup/recovery fix needs explicit migration/fault injection.
- Validate actual max contiguous heap and device power consumption before
  the feature and the separate Native Apps framework are released.

## Verified results (2026-11-03)

- Full CMake host build: PASS.
- CTest: 1125/1125 PASS, 28.35 seconds (includes 8 new gate tests).
- ESP32-C3 X3/X4 PlatformIO default: PASS.
  Flash 5,679,925 / 6,553,600 bytes (86.7%).
  Static RAM 66,904 / 327,680 bytes (20.4%).
  SHA-256 image 6e268c8db29c5808eaf1de1c66f7d30da187431ceb13cd12525ec2b215cae8a8.
- X4 Pro metrics and acceptance are reported only after actual build success.
- All images/build logs stay in local validation artifacts, not in OTA.
