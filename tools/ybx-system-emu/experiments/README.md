# ESP32-S3 ROM DRAM experiment — NOT A FIX

Date: 2026-10-29. Source: Espressif QEMU `febae18`.

Hypothesis: QEMU loads only `esp32s3_rev0_rom.bin` into instruction ROM, but the toolchain ROM ELF includes a 1460-byte DRAM segment at `0x3fcd7000`. Missing ROM data might cause the previously observed debug exception at `0x40049057` (`usb_uart_device_tx_one_char`).

Change in `0002-s3-rom-dram-experiment.patch`: load the local ROM ELF's 1460-byte DRAM segment separately through QEMU's `load_image_targphys_as`. The external binary `esp32s3_rev0_rom_dram.bin` was generated locally and must **not** be committed. This is an exploratory QEMU patch only, not applied to firmware.

**Observed result:** QEMU built and executed with the additional ROM data. The instruction trace still contains the debug exception vector at `0x40000280` / `0x40000288` and the same USB UART function at `0x40049057`. There is no YBX UART output and no application startup evidence; the process timed out after seven seconds.

Trace: `~/Projects/YBX/artifacts/ybx-esp32-system-emulation-20261029/qemu-boot-diagnostics/s3-dram-fixed-exec.log`.

**Decision:** Experiment rejected as an effective boot fix. Patch reverted from the actual QEMU worktree and retained here only as a reproducible hypothesis. Do not deploy, assume ROM completeness, or count this as a passing emulation gate.

Next: investigate why ROM UART's output pointer is invalid/exceptional by inspecting ESP32-S3 ROM runtime initialization, reset peripherals and specific exception registers; avoid bypassing ROM initialization.

## 0004 — forced DRAM initializer load (rejected)

`0004-s3-rom-initializer-trial.patch` is a discarded experiment against
Espressif QEMU's `hw/xtensa/esp32s3.c` (base `febae18`, after accepted
C3 GDMA + USB Serial/JTAG patches).

The patch loads generated local ROM writable-section values into RAM before
Xtensa CPU reset. HMP inspection confirmed that the RAM pointer at
`0x3fceffe4` was initially `0x3fcef6a4`, but ROM reset overwritten it
with zero, leaving the same `LoadProhibited` at `0x400494ff`.
It must NOT be added to `apply_qemu_patches.sh`.

**Superseding working solution:** `../extract_rom_with_sections.py`
reconstructs the guest ROM's **copy-source addresses** using the local
Espressif ELF; the original (unchanged) accepted-patch QEMU then proceeds
beyond the previous fatal ROM handler. Evidence and remaining acceptance
gates are in `../QEMU_BRINGUP.md`. No hardware or production firmware
was changed.

## 0006 — minimal S3 ADC one-shot handshake (experimental, not accepted)

`0006-s3-adc-oneshot-handshake.patch` applies on top of accepted patches
0001, 0003 and 0005 against isolated Espressif QEMU. Its C code implements
only the SENS ADC1/ADC2 `MEAS_CTRL2` start/done-bit handshake, with dummy
zero result and reset-state handling. It is **not a valid electrical ADC**,
and must not be used for accuracy or hardware validation.

With the exact factory X4 Pro ROM correctly reconstructed (including
`.rodata.interface`), normal QEMU stalls inside ESP-IDF
`read_cal_channel()`, waiting for `SENS_MEAS1_DONE_SAR` bit 16 at
`0x6000880c`. This experiment lets the guest progress to repeated
ESP-IDF 5 I2C `ESP_ERR_INVALID_STATE` errors. The experimental patch
compiles, and 18s guest tracing demonstrates progress from ADC calibration
to I2C. It remains **outside** `apply_qemu_patches.sh` pending a full
faithful ADC model. The live isolated local QEMU build currently includes it
for diagnostic experiments; the accepted upstream checkout does not.

See `../QEMU_BRINGUP.md` for evidence, local ROM hashes, flags and
remaining acceptance blockers. Never change production YBX firmware to
work around virtual hardware behavior.
