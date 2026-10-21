#!/usr/bin/env python3
"""Build and inspect a PRIVATE, UNQUALIFIED FM-1 package. Never auto-install it.

The application is NIGHTFALL's. This development-only package reuses the
GPL-3.0-only FM-1 packer and RAM update loader from Felucca, plus the
Apache-2.0 JieLi SPL/config files. It is not a release or a safe-to-flash
artifact: USB enumeration, the updater entry and recovery are untested.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BOARD = Path(os.environ.get("FM1_BOARD_SUPPORT", ROOT.parent / "Felucca")).resolve()
SDK = Path(os.environ.get("AC79_SDK", ROOT.parent / "fw-AC79_AIoT_SDK")).resolve()
APP = ROOT / "build/native-recovery/nightfall-app.bin"
LOADER = BOARD / "build/loader/ota.bin"
OUT = ROOT / "build/experimental/NIGHTFALL-DEV-999-NOFLASH.fwsc"
PRODUCT = "FM-1_999"  # private development identity, not an assigned release ID


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUT)
    args = parser.parse_args()
    pack = load_module(BOARD / "tools/fm1pkg_make.py", "fm1pkg_make")
    install = load_module(BOARD / "tools/fm1_install.py", "fm1_install")
    pack.SDK = SDK
    app = APP.read_bytes()
    loader = LOADER.read_bytes()
    if not app.startswith(bytes.fromhex("04818000")) or len(app) > pack.APP_SLOT:
        raise SystemExit("NIGHTFALL image failed entry/slot checks")
    usb_name = "NIGHTFALL FM-1".encode("utf-16le")
    if PRODUCT.encode("ascii") not in app or usb_name not in app:
        raise SystemExit("recovery-MIDI identity/branding is absent from the app")
    if b"FELUCCA-LOADER-1" not in loader:
        raise SystemExit("the RAM loader is not the expected tested FM-1 loader")

    flash = bytearray(pack.flash_image(app, pack.KEY))
    # The reused packer adds a human-readable config tag; replace it with our
    # own equal-length tag and repair the JLFS data/entry CRCs. The actual
    # NIGHTFALL app and its UI/audio implementation are not copied from Felucca.
    spl = pack.sdk_file("uboot.boot")
    isd_off = 0xA0 + len(spl)
    tag_off = isd_off + len(pack.key_blob(pack.KEY))
    old, new = b"[FELUCCA]", b"NIGHTFALL"
    if flash[tag_off:tag_off + len(old)] != old:
        raise SystemExit("unexpected isd_config tag layout")
    flash[tag_off:tag_off + len(old)] = new
    isd = flash[isd_off:tag_off + len(new) + 2]
    head_entry = bytearray(flash[64:96])
    pack.enc(head_entry, 0, 32)
    if head_entry[16:30] != b"isd_config.ini":
        raise SystemExit("unexpected JLFS isd_config entry")
    struct.pack_into("<H", head_entry, 2, pack.crc16(isd))
    struct.pack_into("<H", head_entry, 0, pack.crc16(head_entry[2:]))
    pack.enc(head_entry, 0, 32)
    flash[64:96] = head_entry

    raw = pack.ufw(bytes(flash), loader, PRODUCT)
    if install.product_of(raw) != PRODUCT:
        raise SystemExit("package product identity round-trip failed")
    logical = install.logical_image(raw)
    hdr = bytearray(logical[:64])
    pack.enc(hdr, 0, 64)
    if pack.crc16(hdr[2:]) != struct.unpack_from("<H", hdr)[0]:
        raise SystemExit("UFW header CRC failed")
    if struct.unpack_from("<I", hdr, 4)[0] != len(logical):
        raise SystemExit("UFW logical size failed")
    entries = bytearray(logical[64:224])
    for i in range(2):
        entry = bytearray(entries[i * 80:(i + 1) * 80])
        pack.enc(entry, 0, 80)
        offset, size = struct.unpack_from("<II", entry, 8)
        expected = struct.unpack_from("<H", entry, 4)[0]
        if offset + size > len(logical) or pack.crc16(logical[offset:offset + size]) != expected:
            raise SystemExit(f"UFW payload {i} failed bounds or CRC")
    if pack.crc16(entries) != struct.unpack_from("<H", hdr, 2)[0]:
        raise SystemExit("UFW entry-list CRC failed")
    region = bytearray(flash[0x4000:0x4000 + 0x120 + pack.APP_SLOT])
    pack.sfc(region, 0, len(region), 0, pack.KEY)
    if region[0x120:0x120 + len(app)] != app:
        raise SystemExit("encrypted app failed decryption round-trip")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(raw)
    print(f"EXPERIMENTAL / DO NOT FLASH: {args.output}")
    print(f"product={PRODUCT} bytes={len(raw)} sha256={hashlib.sha256(raw).hexdigest()}")
    print("Verified: UFW CRCs, identity, app slot, encrypted app round-trip.")
    print("NOT verified: device boot, USB, update entry, rollback or recovery.")


if __name__ == "__main__":
    main()
