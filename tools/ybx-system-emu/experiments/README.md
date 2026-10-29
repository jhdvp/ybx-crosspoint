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

## 0007 — ESP32-C3/S3 I2C master integration (experimental)

`0007-c3-s3-i2c-master.patch` reuses QEMU's existing classic ESP32
I2C model, with an explicit C3/S3 command encoding mode:
RESTART 6, WRITE 1, READ 3, STOP 2, END 4 (ESP-IDF 5). The classic
ESP32 command encoding remains the default and is unaffected.
It adds register `I2C_FIFO_ST` at 0x14, routes C3 I2C0 and
S3 I2C0/I2C1 through their real MMIO/IRQ addresses, and checks
the final command register index boundary.

The patch is a **diagnostic experiment**, not yet an accepted QEMU
hardware model. It is based on the isolated QEMU tree **with**
`0006-s3-adc-oneshot-handshake.patch` already applied. Apply 0006
before 0007. Both targets compiled successfully with Meson/Ninja.
`probe_i2c_qtest.py` passed on both real compiled QEMU binaries,
testing no-slave NACK, IRQ, clearing IRQ, command DONE,
and RESTART/STOP opcodes. Real S3 YBX firmware MMIO trace verifies
the firmware sends GT911 address 0x5D, receives NACK, and handles IRQ;
it does NOT establish actual GT911 behavior. C3 aborts via an
interrupt watchdog before accessing I2C. Logs and JSON reports
are documented in `../QEMU_BRINGUP.md`.

Keep absent I2C slaves absent. The X4 Pro requires separate
BM8563 RTC at 0x51, CW2017 at 0x63 and power-gated GT911 at 0x5D
models, each with faithful protocol/interrupt/power semantics.

## 0008 — C3 ADC one-shot bit handshake (experimental)

`0008-c3-adc-oneshot-handshake.patch` is an **unaccepted**
RISC-V/C3-only prototype on top of experiment 0007 in isolated
Espressif QEMU. It maps C3 `APB_SARADC_ONETIME_SAMPLE` 0x60040020,
`INT_RAW` 0x60040044 and `INT_CLR` 0x6004004c and reports ADC1
DONE only after START. It always returns a dummy zero sample.
It neither models analogue input nor proves ADC calibration or
guest application readiness.

Real compiled-QEMU MMIO QTest passed via
`probe_c3_adc_qtest.py`; full 58/58 host tests pass. Guest UART
showed no previous watchdog within a 10-second bounded run, but also
did not reach normal YBX app UART in that window. Exact source
revision was rebuilt, but C3 factory IROM did **not** binary-match;
do not overinterpret C3 ELF line mappings. See
`../QEMU_BRINGUP.md` for logs, hashes, register semantics, and
strict boot acceptance requirements.

## 0009 — X4 Pro BM8563 RTC + CW2017 gauge, explicit S3 machine opt-in

`0009-s3-optin-bm8563-cw2017.patch` introduces
`hw/i2c/ybx_x4pro_i2c.c` and uses Espressif QEMU's existing
I2C bus model from 0007. Do **not** add these virtual devices to
generic S3 by default. To enable this simulation:

```text
-machine esp32s3,ybx-x4pro-i2c=on
```

The option belongs to **-machine**; `-global xtensa.esp32s3...`
does not work (QEMU reports an invalid global class). The YBX
emulator runner now supports `--x4pro-i2c` and records its
attachment in `report.json`. C3 rejects the option. QTest
exercises both negative and positive configurations and full
I2C register read/write and CW2017 80-byte profile upload.
It does not imply verified hardware/boot readiness.

Modes and constraints: synthetic 70% battery/3800mV (overridable
on the CW2017 QEMU device), real host-synchronized BCD RTC with
basic writable registers, no charger GPIO21 status, no GT911,
no power gating or I2C wake interrupts, no power-accurate analogue
inputs. See `../QEMU_BRINGUP.md` for validated chip addresses,
QTest evidence, I2C device limitations and boot/acceptance status.
QEMU's current `esp32_gpio.c` write handler is a stub; proper
GT911 simulation requires GPIO2 (active-low power), GPIO4 (reset),
GPIO10 (interrupt) behavior first.

## 0010 — opt-in GPIO2/4/10 output and enable MMIO

`0010-s3-optin-gpio-output-registers.patch` (after 0009) adds
S3 pin output callbacks and basic GPIO_OUT/ENABLE/W1TS/W1TC MMIO
to the otherwise no-op Espressif GPIO parent model.
Only enabled with `-machine esp32s3,ybx-x4pro-i2c=on`.
Tested against compiled QEMU with `probe_x4pro_gpio_qtest.py`:
normal S3 still ignores writes, opt-in S3 updates both GPIO
register banks, preserves boot strap and toggles GPIO2/4/10
direction/output correctly. This is GPIO MMIO groundwork only.

## 0011 — GT911 GPIO power/reset/address I2C verified; input still pending

`0011-s3-optin-gt911-power-gating.patch` (after 0010)
compiles an optional Goodix GT911 model on S3 I2C0, with GPIO2
active-low power, GPIO4 reset, GPIO10 address selection, no
fabricated touches and idle status at 0x814E. Its real compiled-QEMU
**deterministic QTest passes** after fixing the runner to use
`-accel qtest` alongside `-qtest stdio`. This unlocks the
`clock_step` command, previously rejected due to missing
QTest accelerator. The script checks 0x5D/0x14, NACK while
unpowered/in reset, minimum reset and ready delays, product
registers and no contacts. JSON evidence
`qa-20261029/s3-x4pro-gt911-qtest.json`.

A standalone YBX runner opt-in `--x4pro-gt911` requires
`--x4pro-i2c`; the generic S3 default is unchanged. This
validates **synthetic device handshake and MMIO behavior**, not
touch-event injection, input GPIO interrupts, full Goodix setup,
e-ink panel output, SD mount or full guest ready. 0011 remains
EXPERIMENTAL rather than added to the default patch chain.
See `../QEMU_BRINGUP.md` for the IROM-execution evidence and
exact factory/build SHA matching.
