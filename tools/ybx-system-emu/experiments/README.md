# ESP32-S3 ROM DRAM experiment — NOT A FIX

Date: 2026-10-29. Source: Espressif QEMU `febae18`.

Hypothesis: QEMU loads only `esp32s3_rev0_rom.bin` into instruction ROM, but the toolchain ROM ELF includes a 1460-byte DRAM segment at `0x3fcd7000`. Missing ROM data might cause the previously observed debug exception at `0x40049057` (`usb_uart_device_tx_one_char`).

Change in `0002-s3-rom-dram-experiment.patch`: load the local ROM ELF's 1460-byte DRAM segment separately through QEMU's `load_image_targphys_as`. The external binary `esp32s3_rev0_rom_dram.bin` was generated locally and must **not** be committed. This is an exploratory QEMU patch only, not applied to firmware.

**Observed result:** QEMU built and executed with the additional ROM data. The instruction trace still contains the debug exception vector at `0x40000280` / `0x40000288` and the same USB UART function at `0x40049057`. There is no YBX UART output and no application startup evidence; the process timed out after seven seconds.

Trace: `~/Projects/YBX/artifacts/ybx-esp32-system-emulation-20261029/qemu-boot-diagnostics/s3-dram-fixed-exec.log`.

**Decision:** Experiment rejected as an effective boot fix. Patch reverted from the actual QEMU worktree and retained here only as a reproducible hypothesis. Do not deploy, assume ROM completeness, or count this as a passing emulation gate.

Next: investigate why ROM UART's output pointer is invalid/exceptional by inspecting ESP32-S3 ROM runtime initialization, reset peripherals and specific exception registers; avoid bypassing ROM initialization.
