#!/usr/bin/env python3
"""Observe real factory S3 app SPI2 PIO counters, NOT EPD pixels or UI readiness.

Runs inside offline Docker with a copied SD test disk, real CPU (no qtest
accelerator). No production firmware, media, or device hardware is modified.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
import subprocess
import time
from tempfile import TemporaryDirectory
from probe_x4pro_touch_injection_qtest import qmp_connect

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("qemu-bin","rom","flash","sd-image","out"):
        p.add_argument("--"+key,type=Path,required=True)
    p.add_argument("--seconds",type=int,default=9)
    a=p.parse_args()
    with TemporaryDirectory(prefix="ybx-spi2-factory-") as temp:
        root=Path(temp)
        bios=root/"bios";bios.mkdir()
        shutil.copyfile(a.rom,bios/"esp32s3_rev0_rom.bin")
        shutil.copyfile(a.sd_image,root/"test-sd.img")
        # Never write to the actual factory flash file; QEMU VM gets
        # an isolated copy of the image as the test's MTD.
        shutil.copyfile(a.flash,root/"factory-flash.bin")
        socket=root/"qmp.sock"
        cmd=[
            str(a.qemu_bin),
            "-machine","esp32s3,ybx-x4pro-i2c=on,ybx-x4pro-gt911=on,"
                       "ybx-x4pro-sd-power=on,ybx-x4pro-spi2=on",
            "-m","8M","-global","ssi_psram.is_octal=on",
            "-nographic","-monitor","none",
            "-serial","null","-serial","null","-serial","null","-serial","stdio",
            "-qmp",f"unix:{socket},server=on,wait=off",
            "-L",str(bios),
            "-drive",f"file={root/'factory-flash.bin'},if=mtd,format=raw",
            "-drive",f"file={root/'test-sd.img'},if=sd,format=raw",
        ]
        with (root/"qemu.out").open("w") as stdout, (root/"qemu.err").open("w") as stderr:
            process=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,
                                     stdout=stdout,stderr=stderr)
            client=None
            try:
                client,reader,qmp=qmp_connect(socket)
                time.sleep(a.seconds)
                result={}
                for property in ("bytes-sent","transactions","last-byte"):
                    value=qmp("qom-get",{"path":"/machine/soc/spi2",
                                         "property":property})
                    if "return" not in value:raise AssertionError(value)
                    result[property]=value["return"]
            finally:
                if client:client.close()
                process.terminate()
                try:process.communicate(timeout=3)
                except subprocess.TimeoutExpired:process.kill();process.communicate()
        uart=(root/"qemu.out").read_text(errors="replace")
        err=(root/"qemu.err").read_text(errors="replace")
    report={
        "qemu_binary_sha256":hashlib.sha256(a.qemu_bin.read_bytes()).hexdigest(),
        "factory_flash_sha256":hashlib.sha256(a.flash.read_bytes()).hexdigest(),
        "executed_seconds":a.seconds,
        "spi2_pio_counters":result,
        "factory_application_uart_seen":"Preferences.cpp" in uart,
        "rom_boot_seen":"ESP-ROM:" in uart,
        "qemu_host_error_excerpt":err[-1500:],
        "qemu_uart_excerpt":uart[-1000:],
        "guest_spi2_pio_activity_detected":result["transactions"]>0,
        "dma_gdma_emulated":False,
        "epd_controller_verified":False,
        "physical_screen_pixels_verified":False
    }
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))

if __name__=="__main__": main()
