#!/usr/bin/env python3
"""Run REAL YBX merged ESP32 images under the open Espressif QEMU fork.

No credentials, no network, no real hardware or production writes.
This is independent of the binary-only Espressif ESP-EMU v0.51.0 runner.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import argparse
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import sys
from run import validate_image

EMU = {
    "c3": ("riscv32", "esp32c3"),
    "s3": ("xtensa", "esp32s3"),
}
FLASH_BYTES = 16*1024*1024


def validate_sd_image(image: Path) -> None:
    """Accept a disposable FAT superfloppy, not arbitrary SD contents."""
    if not image.is_file() or image.stat().st_size < 512:
        raise ValueError("test SD must be an existing FAT16/FAT32 disk image")
    with image.open("rb") as stream:
        boot = stream.read(512)
    marker = boot[54:62] if boot[54:59] == b"FAT16" else boot[82:90]
    if (boot[510:512] != bytes((0x55, 0xaa)) or
            marker[:5] not in (b"FAT16", b"FAT32") or
            int.from_bytes(boot[11:13], "little") not in (512, 1024, 2048, 4096) or
            boot[13] == 0):
        raise ValueError("test SD must have a valid FAT16/FAT32 boot sector")


def timeout_text(data: bytes | str | None) -> str:
    """TimeoutExpired may contain bytes even with text=True."""
    if data is None:
        return ""
    return data.decode("utf-8", "replace") if isinstance(data, bytes) else data


def classify_qemu_log(log: str, exit_code: int) -> dict:
    """Only guest UART can establish ROM/app boot, not a running QEMU CPU."""
    rom = "ESP-ROM:" in log
    flash = ("SPI_FAST_FLASH_BOOT" in log or "SPI_FLASH_BOOT" in log) and "entry 0x" in log
    app = any(marker in log for marker in (
        "Preferences.cpp", "Wire.cpp", "sdmmc_periph:", "Arduino",
        "esp_psram:", "cpu_start:",
    ))
    guest_panic = "Guru Meditation Error" in log or "panic'ed" in log
    gdma = "esp32c3_generic_int_reg" in log
    abort = gdma or any(marker in log for marker in (
        "Assertion ", "assertion failed", "Bail out!", "unknown type",
        "ERROR:", "corrupted size vs. prev_size", "malloc():", "double free",
    )) or exit_code not in (0, 124)
    timed_out = exit_code == 124 or "terminating on signal 15" in log
    return {
        "rom_seen": rom,
        "spi_flash_boot_seen": flash,
        "guest_driver_messages": app,
        "gdma_assertion": gdma,
        "host_heap_corruption": any(marker in log for marker in (
            "corrupted size vs. prev_size", "malloc():", "double free",
        )),
        "qemu_abort": abort,
        "guest_panic_detected": guest_panic,
        "qemu_timeout": timed_out,
        "boot_evidence": ("application_uart" if rom and flash and app else
                          "rom_uart" if rom else "no_guest_rom_uart"),
    }


def run_one(chip: str, image: Path, qemu: Path, output: Path,
            seconds: int, sd_image: Path | None, docker_image: str,
            s3_rom_image: Path | None = None,
            usb_serial: bool = False,
            c3_rom_image: Path | None = None,
            s3_octal_psram_mb: int | None = None,
            x4pro_i2c: bool = False) -> dict:
    sha256=validate_image(image)
    rom_name = "esp32c3-rom.bin" if chip=="c3" else "esp32s3_rev0_rom.bin"
    rom=qemu/"build-ybx"/rom_name
    if not rom.is_file() or not 0 < rom.stat().st_size <= 0x60000:
        raise ValueError(f"missing ROM image: {rom}. Extract from local Espressif ROM ELF first")
    custom_rom = (s3_rom_image if chip == "s3" else c3_rom_image)
    rom_source = custom_rom or rom
    if rom_source.stat().st_size != 0x60000:
        raise ValueError(f"unexpected ROM size: {rom_source} (expected 0x60000 bytes)")
    with rom_source.open("rb") as rom_stream:
        rom_digest = hashlib.file_digest(rom_stream, "sha256").hexdigest()
    output.mkdir(parents=True,exist_ok=True)
    with TemporaryDirectory(prefix=f"ybx-qemu-{chip}-") as temp:
        scratch=Path(temp)
        source=image.read_bytes()
        (scratch/"flash.bin").write_bytes(source+b"\xff"*(FLASH_BYTES-len(source)))
        if chip=="s3" and sd_image:
            validate_sd_image(sd_image)
            shutil.copy2(sd_image,scratch/"sd.img")
        bios_dir = "/qemu/build-ybx"
        if custom_rom:
            custom_bios = scratch / "bios"
            custom_bios.mkdir()
            shutil.copyfile(rom_source, custom_bios / rom_name)
            bios_dir = "/work/bios"
        target,machine=EMU[chip]
        if x4pro_i2c:
            if chip != "s3":
                raise ValueError("--x4pro-i2c is supported only on ESP32-S3")
            machine += ",ybx-x4pro-i2c=on"
        cmd=[
            "docker","run","--rm","--network","none",
            "--platform","linux/arm64",
            "--mount",f"type=bind,src={qemu},dst=/qemu,readonly",
            "--mount",f"type=bind,src={scratch},dst=/work",
            "--workdir","/work",docker_image,
            "timeout","-k","2s",f"{seconds}s",
            f"/qemu/build-ybx/qemu-system-{target}",
            "-machine",machine,"-nographic",
            "-monitor","none","-icount","3","-L",bios_dir,
            "-drive","file=/work/flash.bin,if=mtd,format=raw",
        ]
        if chip=="s3" and sd_image:
            cmd+=["-drive","file=/work/sd.img,if=sd,format=raw"]
        if chip == "s3" and s3_octal_psram_mb is not None:
            if s3_octal_psram_mb not in (8, 16):
                raise ValueError("unsupported virtual Octal PSRAM size")
            cmd += ["-m", f"{s3_octal_psram_mb}M",
                    "-global", "ssi_psram.is_octal=on"]
        if usb_serial:
            for _ in range(2 if chip == "c3" else 3):
                cmd += ["-serial", "null"]
            cmd += ["-serial", "stdio"]
        try:
            process=subprocess.run(cmd,stdin=subprocess.DEVNULL,
                                   capture_output=True,text=True,
                                   encoding="utf-8",errors="replace",
                                   timeout=seconds+30)
            log=process.stdout+"\n"+process.stderr
            exit_code=process.returncode
        except subprocess.TimeoutExpired as exc:
            log=timeout_text(exc.stdout)+"\n"+timeout_text(exc.stderr)
            log+="\nHOST WATCHDOG TIMEOUT\n"
            exit_code=124
        (output/f"qemu-{chip}.log").write_text(log)
    evidence=classify_qemu_log(log, exit_code)
    return {
        "chip":chip,"machine":machine,
        **evidence,
        "flash_source_sha256":sha256,"flash_source":str(image),
        "rom_sha256":rom_digest,"rom_source":str(rom_source),
        "runtime_exit_code":exit_code,"duration_limit_s":seconds,
        "usb_serial_routed":usb_serial,
        "virtual_octal_psram_mb": (s3_octal_psram_mb if chip == "s3" else None),
        "virtual_x4pro_i2c_attached": bool(x4pro_i2c and chip == "s3"),
        "virtual_x4pro_gt911_attached": False,
        "sdmmc_error":"sdmmc_host_reset returned 0x107" in log,
        "rom_present":True,
        "sdcard_attached":bool(chip=="s3" and sd_image),
        "sd_mount_verified":False, # only set after real FAT file read/write checks
        "epaper_verified":False,
        "wifi_tls_verified":False,
        "log":f"qemu-{chip}.log",
        "network":"docker --network none",
        "command":cmd,
    }

def main()->int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--chip",choices=list(EMU)+["both"],default="s3")
    p.add_argument("--c3-image",type=Path)
    p.add_argument("--s3-image",type=Path)
    p.add_argument("--sd-image",type=Path)
    p.add_argument("--s3-rom-image",type=Path,
                   help="locally extracted S3 ROM with reset-copy data sources")
    p.add_argument("--c3-rom-image",type=Path,
                   help="locally extracted C3 ROM with reset-copy data sources")
    p.add_argument("--usb-serial",action="store_true",
                   help="route USB Serial/JTAG to host using QEMU patch 0005")
    p.add_argument("--s3-octal-psram-mb",type=int,choices=(8,16),
                   help="attach Octal PSRAM, 8 or 16 MiB, to S3 QEMU")
    p.add_argument("--x4pro-i2c", action="store_true",
                   help="opt-in synthetic BM8563/CW2017 devices; GT911 not implemented")
    p.add_argument("--qemu-root",type=Path,
                   default=Path.home()/".local/state/ybx-emulator-upstream/qemu")
    p.add_argument("--docker-image",default="ybx-qemu-builder:20261029")
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--duration",type=int,default=12)
    args=p.parse_args()
    if not 2 <= args.duration <= 60:p.error("duration must be 2..60s")
    if args.x4pro_i2c and args.chip == "c3":
        p.error("--x4pro-i2c requires ESP32-S3")
    if args.chip=="s3" and not args.s3_image:p.error("provide --s3-image")
    if args.chip=="c3" and not args.c3_image:p.error("provide --c3-image")
    if args.chip=="both" and (not args.c3_image or not args.s3_image):
        p.error("both images required")
    root=args.qemu_root.expanduser().resolve(strict=True)
    required_targets = [EMU[chip][0] for chip in (
        ("c3", "s3") if args.chip == "both" else (args.chip,))]
    for target in required_targets:
        if not (root / "build-ybx" / f"qemu-system-{target}").is_file():
            p.error(f"QEMU binary missing: qemu-system-{target}")
    if args.chip in ("s3","both") and not args.sd_image:
        p.error("must pass a disposable FAT test SD image for X4 Pro")
    output=args.out.expanduser().resolve()
    chips=["c3","s3"] if args.chip=="both" else [args.chip]
    results=[]
    for chip in chips:
        image=getattr(args,f"{chip}_image").expanduser().resolve(strict=True)
        sd=(args.sd_image.expanduser().resolve(strict=True)
            if chip=="s3" else None)
        s3_rom = (args.s3_rom_image.expanduser().resolve(strict=True)
                  if chip == "s3" and args.s3_rom_image else None)
        c3_rom = (args.c3_rom_image.expanduser().resolve(strict=True)
                  if chip == "c3" and args.c3_rom_image else None)
        result=run_one(chip,image,root,output,args.duration,sd,
                       args.docker_image,s3_rom,args.usb_serial,c3_rom,
                       args.s3_octal_psram_mb,args.x4pro_i2c)
        print(f"{chip}: exit={result['runtime_exit_code']}, "
              f"ROM={result['rom_seen']}, flash={result['spi_flash_boot_seen']}, "
              f"SDMMC error={result['sdmmc_error']}, "
              f"SD mount={'NOT VERIFIED' if not result['sd_mount_verified'] else 'VERIFIED'}")
        results.append(result)
    report={"generated_at":datetime.now(timezone.utc).isoformat(),
            "emulator":"espressif/qemu open fork",
            "description":"Actual guest machine-code execution; not full Xteink-board acceptance",
            "results":results}
    output.mkdir(parents=True,exist_ok=True)
    (output/"report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print("QEMU report:",output/"report.json")
    # A timed-out VM with no application UART is NOT a passed firmware boot.
    return 0 if all(r["runtime_exit_code"] == 0 and
                    r["rom_seen"] and r["spi_flash_boot_seen"] and
                    r["guest_driver_messages"] and not r["qemu_abort"]
                    and not r["guest_panic_detected"]
                    for r in results) else 1

if __name__=="__main__":
    try:sys.exit(main())
    except (ValueError,OSError,subprocess.SubprocessError) as ex:
        print("ERROR:",ex,file=sys.stderr)
        sys.exit(2)
