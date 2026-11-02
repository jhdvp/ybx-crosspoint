# YBX Reader · No-SD recovery boot path (P1-C)

Base: fix/ybx-stats-atomic-20261103 (draft PR #9).
Isolated work: fix/ybx-nosd-recovery-20261103.

## Defect

The previous setup() displayed a generic "SD card error" when Storage.begin()
failed, then returned. The normal main loop kept accessing settings-dependent
shortcuts, battery operations, input mappings, global sleep logic, and activity
state, despite SETTINGS, APP_STATE and all SD-backed services never having
finished initialization. This is an unsafe partial-boot state.

## Change

- Introduce a separate NoSdRecoveryActivity rendered entirely with built-in
  fonts: title, card instruction, physical-button and touchscreen retry hint,
  and non-destructive status text.
- The SD-failure branch initializes only display, built-in fonts, input and
  the recovery activity. The SD-font scanner is explicitly skipped.
- The first statement of the ordinary main loop guards sdRecoveryBootMode:
  only input update, recovery activity loop and low-power idle delay execute.
  No regular settings, Wi-Fi, plugin, queue, file browser, or screenshot code
  can run during the failed-mount boot.
- Retry is **user-triggered**, never periodically polled. Press OK/Right or
  tap the touchscreen. The RetryGate enforces a 2000 ms cooldown and tolerates
  millis() wraparound.
- A successful Storage.begin() does **not** resume a partially initialized
  setup() or write any files. It invokes ESP.restart(), so the next boot
  follows the entire normal initialization sequence.
- No filesystem formatting, remount "repair", removal, renaming, or rewriting
  is attempted on failure. Users can insert a readable card and retry.

## Verification and limits

- 5 host unit tests for the explicit-input gate, cooldown, wraparound, and
  absence of background probes.
- Hardware/firmware builds for ESP32-C3 and ESP32-S3 and full host regression
  are required before merge. Do not infer a pass from this document alone.
- Actual card removal/insertion on X3/X4/X4 Pro remains a **hardware gate**.
  ESP.restart() on success is intentional. Recovery is limited to initial
  failed SD mount; unplugging an already-mounted card at runtime is a separate
  problem.
- Recovery UI uses English boot-safe strings because SD-stored localization
  preferences were not yet loaded. This path intentionally does not create
  extra files or activate networking.
- No auto-sleep from the normal loop during failed setup; the CPU is moved
  to power-saving mode between user actions. Physical current draw must be
  measured. The device may need manual power-off if no card is available.
- No OTA or production changes were performed.
