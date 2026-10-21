#!/usr/bin/env python3
"""Build NIGHTFALL's original application for the FM-1 CPU.

Uses Felucca's unmodified GPLv3 board-support headers, startup and linker
script as external build dependencies. Does not produce an OTA package: target
linking alone is not sufficient to establish that a firmware is safe to flash.
NF_USB_MODE=midi builds a separate first-board-test image with a USB-MIDI
return/update path.
"""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOARD = Path(os.environ.get("FM1_BOARD_SUPPORT", "/felucca")).resolve()
TC = Path(os.environ.get("JIELI_TOOLCHAIN", "/opt/jieli")).resolve()
USB_MODE = os.environ.get("NF_USB_MODE", "bulk")
if USB_MODE not in ("bulk", "midi"):
    raise SystemExit("NF_USB_MODE must be bulk or midi")
OUT = ROOT / "build" / ("native-recovery" if USB_MODE == "midi" else "native")
OUT.mkdir(parents=True, exist_ok=True)


def run(tool, *args):
    cmd = [str(TC / tool), *map(str, args)]
    subprocess.run(cmd, cwd=ROOT, check=True)


if not (BOARD / "firmware" / "hal" / "fm1_audio.h").exists():
    raise SystemExit("FM1_BOARD_SUPPORT must point to an FM-1 GPL board-support checkout")

hal = BOARD / "firmware" / "hal"
flags = ["-target", "pi32v2", "-std=c11", "-Os", "-DNF_USB_TEST_ID=1", "-ffunction-sections",
         "-fdata-sections", "-fno-builtin", "-Wall", "-Wextra",
         "-Wno-unused-function", "-Wno-unused-variable", "-I", ROOT / "src",
         "-I", hal]
if USB_MODE == "midi":
    flags += ["-DNF_RECOVERY_MIDI=1", "-I", BOARD / "firmware" / "src", "-I", OUT]
    usb_source = (BOARD / "firmware/src/usb.c").read_text()
    if usb_source.count('#include "../hal/fm1_usb.h"') != 1:
        raise SystemExit("upstream USB HAL include changed")
    usb_source = usb_source.replace('#include "../hal/fm1_usb.h"', '#include "fm1_usb.h"')

    def usb_string(label):
        if not label.isascii():
            raise SystemExit("USB descriptor labels must be ASCII")
        values = [str(2 + 2 * len(label)), "3"]
        for char in label:
            values += [str(ord(char)), "0"]
        return "{" + ", ".join(values) + "}"

    for name, label, expected in (("STR1", "NIGHTFALL", 1),
                                  ("STR2", "NIGHTFALL FM-1", 2)):
        usb_source, found = re.subn(r"static const uint8_t " + name + r"\[\] = \{.*?\};",
                                    "static const uint8_t " + name + "[] = " + usb_string(label) + ";",
                                    usb_source, flags=re.S)
        if found != expected:
            raise SystemExit(f"upstream USB {name} descriptor changed ({found})")
    (OUT / "usb_nightfall.c").write_text(usb_source)
objects = []
for source in ("src/core.c", "src/fx.c", "src/city.c", "src/project.c", "src/protocol.c", "src/link.c", "src/store.c",
               "target/freestanding.c", "target/fm1_app.c", *(("target/nf_usb.c",) if USB_MODE == "bulk" else ())):
    obj = OUT / (Path(source).stem + ".o")
    run("pi32v2/bin/clang", *flags, "-c", ROOT / source, "-o", obj)
    objects.append(obj)
for source in ("firmware/crt0.S", "firmware/hal/fm1_vec.S", "firmware/hal/fm1_isr.S"):
    obj = OUT / (Path(source).stem + ".o")
    run("pi32v2/bin/clang", "-target", "pi32v2", "-c", BOARD / source, "-o", obj)
    objects.append(obj)
elf = OUT / "nightfall.elf"
run("pi32v2/bin/ld", "--gc-sections", "-T", BOARD / "firmware" / "app.ld",
    *objects, "-o", elf)
symbols = subprocess.check_output([str(TC / "common/bin/objdump"), "-t", str(elf)], text=True)


def address(symbol):
    match = re.search(r"^([0-9a-f]+) .*\s" + re.escape(symbol) + r"$", symbols, re.M)
    if not match:
        raise SystemExit(f"missing linker symbol {symbol}")
    return int(match.group(1), 16)


xip = 0x02000120
if address("_start") != xip:
    raise SystemExit("bad native entry address")
image = bytearray()
for section, symbol in ((".text", None), (".ram_text", "_rt_load"),
                        (".data", "_data_load")):
    binary = OUT / (section[1:] + ".bin")
    subprocess.run([str(TC / "common/bin/objcopy"), "-O", "binary", "-j",
                    section, str(elf), str(binary)], check=True)
    content = binary.read_bytes() if binary.exists() else b""
    offset = address(symbol) - xip if symbol else 0
    if offset < len(image):
        raise SystemExit(f"{section} overlaps prior image")
    image.extend(b"\xff" * (offset - len(image)))
    image.extend(content)
image.extend(b"\xff" * (-len(image) % 4))
if image[:4] != bytes.fromhex("04818000"):
    raise SystemExit("native image lacks expected FM-1 entry stub")
if len(image) > 0x8DFBC:
    raise SystemExit("native app exceeds the FM-1 app slot")
headroom = 0x01C1A000 - address("_bss_end")
pool_room = 0x01C74000 - address("_pool_end")
if headroom < 4096 or pool_room < 8192:
    raise SystemExit(f"insufficient RAM headroom: {headroom}, pool: {pool_room}")
(OUT / "nightfall-app.bin").write_bytes(image)
print(f"NIGHTFALL app: {len(image)} B; RAM spare {headroom} B; pool spare {pool_room} B")
if USB_MODE == "midi":
    print("USB-MIDI update/identity path linked for first-board testing, still TEST VID/PID")
else:
    print("USB bulk transport is linked with a TEST identity; not enumerated on hardware")
print("NOR journal compiles but is not bound to target flash; linker removes it")
print("Native binary built but NOT qualified or packaged for flashing")
