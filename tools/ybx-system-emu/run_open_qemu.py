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

def run_one(chip: str, image: Path, qemu: Path, output: Path,
            seconds: int, sd_image: Path | None, docker_image: str) -> dict:
    sha256=validate_image(image)
    rom_name = "esp32c3-rom.bin" if chip=="c3" else "esp32s3_rev0_rom.bin"
    rom=qemu/"build-ybx"/rom_name
    if not rom.is_file() or not 0 < rom.stat().st_size <= 0x60000:
        raise ValueError(f"missing ROM image: {rom}. Extract from local Espressif ROM ELF first")
    output.mkdir(parents=True,exist_ok=True)
    with TemporaryDirectory(prefix=f"ybx-qemu-{chip}-") as temp:
        scratch=Path(temp)
        source=image.read_bytes()
        (scratch/"flash.bin").write_bytes(source+b"\xff"*(FLASH_BYTES-len(source)))
        if chip=="s3" and sd_image:
            with sd_image.open("rb") as sd:
                sd.seek(54)
                marker=sd.read(8)
            if b"FAT16" not in marker and b"FAT32" not in marker:
                raise ValueError("test SD must contain a FAT16/FAT32 boot sector")
            shutil.copy2(sd_image,scratch/"sd.img")
        target,machine=EMU[chip]
        cmd=[
            "docker","run","--rm","--network","none",
            "--platform","linux/arm64",
            "--mount",f"type=bind,src={qemu},dst=/qemu,readonly",
            "--mount",f"type=bind,src={scratch},dst=/work",
            "--workdir","/work",docker_image,
            "timeout","-k","2s",f"{seconds}s",
            f"/qemu/build-ybx/qemu-system-{target}",
            "-machine",machine,"-nographic",
            "-monitor","none","-icount","3","-L","/qemu/build-ybx",
            "-drive","file=/work/flash.bin,if=mtd,format=raw",
        ]
        if chip=="s3" and sd_image:
            cmd+=["-drive","file=/work/sd.img,if=sd,format=raw"]
        try:
            process=subprocess.run(cmd,stdin=subprocess.DEVNULL,
                                   capture_output=True,text=True,
                                   encoding="utf-8",errors="replace",
                                   timeout=seconds+30)
            log=process.stdout+"\n"+process.stderr
            exit_code=process.returncode
        except subprocess.TimeoutExpired as exc:
            log=(exc.stdout or b"").decode("utf-8","replace")+"\n"+(
                (exc.stderr or b"").decode("utf-8","replace"))
            log+="\nHOST WATCHDOG TIMEOUT\n"
            exit_code=124
        (output/f"qemu-{chip}.log").write_text(log)
    return {
        "chip":chip,"machine":machine,
        "flash_source_sha256":sha256,"flash_source":str(image),
        "runtime_exit_code":exit_code,"duration_limit_s":seconds,
        "rom_seen":"ESP-ROM:" in log,
        "spi_flash_boot_seen":"SPI_FAST_FLASH_BOOT" in log,
        "guest_driver_messages":any(x in log for x in (
            "Preferences.cpp", "Wire.cpp", "sdmmc", "Arduino")),
        "sdmmc_error":"sdmmc_host_reset returned 0x107" in log,
        "gdma_assertion":"esp32c3_generic_int_reg" in log,
        "qemu_abort":any(marker in log for marker in (
            "Assertion ", "assertion failed", "Bail out!", "unknown type", "ERROR:")),
        "qemu_timeout":"terminating on signal 15" in log,
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
    p.add_argument("--qemu-root",type=Path,
                   default=Path.home()/".local/state/ybx-emulator-upstream/qemu")
    p.add_argument("--docker-image",default="ybx-qemu-builder:20261029")
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--duration",type=int,default=12)
    args=p.parse_args()
    if not 2 <= args.duration <= 60:p.error("duration must be 2..60s")
    if args.chip=="s3" and not args.s3_image:p.error("provide --s3-image")
    if args.chip=="c3" and not args.c3_image:p.error("provide --c3-image")
    if args.chip=="both" and (not args.c3_image or not args.s3_image):
        p.error("both images required")
    root=args.qemu_root.expanduser().resolve(strict=True)
    if not (root/"build-ybx/qemu-system-xtensa").exists():
        p.error("QEMU binary missing: build QEMU first")
    if args.chip in ("s3","both") and not args.sd_image:
        p.error("must pass a disposable FAT test SD image for X4 Pro")
    output=args.out.expanduser().resolve()
    chips=["c3","s3"] if args.chip=="both" else [args.chip]
    results=[]
    for chip in chips:
        image=getattr(args,f"{chip}_image").expanduser().resolve(strict=True)
        sd=(args.sd_image.expanduser().resolve(strict=True)
            if chip=="s3" else None)
        result=run_one(chip,image,root,output,args.duration,sd,args.docker_image)
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
    return 0 if all(r["rom_seen"] and r["spi_flash_boot_seen"]
                    and r["guest_driver_messages"] and not r["qemu_abort"]
                    for r in results) else 1

if __name__=="__main__":
    try:sys.exit(main())
    except (ValueError,OSError,subprocess.SubprocessError) as ex:
        print("ERROR:",ex,file=sys.stderr)
        sys.exit(2)
