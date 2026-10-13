#!/usr/bin/env python3
"""Merge the bootloader, partition table, MothBoot, and MothDiag into one image."""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "tools" / "mothboot" / ".pio" / "build" / "mothboot"
BOOT = BUILD / "bootloader.bin"
PARTS = BUILD / "partitions.bin"
FACTORY = BUILD / "firmware.bin"
DIAG = ROOT / "tools" / "blemidi-diag" / ".pio" / "build" / "mothdiag" / "firmware.bin"
OUT = ROOT / "releases" / "mothboot-0.1.1-full.bin"


def ota_select(seq: int, state: int) -> bytes:
    crc = zlib.crc32(struct.pack("<I", seq)) & 0xFFFFFFFF
    entry = struct.pack("<I20sII", seq, b"\x00" * 20, state, crc)
    sector = entry + b"\xff" * (0x1000 - len(entry))
    return sector + sector


def main() -> int:
    missing = [str(p) for p in (BOOT, PARTS, FACTORY, DIAG) if not p.exists()]
    if missing:
        sys.stderr.write("missing build output:\n  " + "\n  ".join(missing) + "\n")
        return 1
    diag_blob = DIAG.read_bytes()
    end = 0x190000 + len(diag_blob)
    image = bytearray(b"\xff" * end)
    pieces = (
        (0x0, BOOT.read_bytes()),
        (0x8000, PARTS.read_bytes()),
        (0xE000, ota_select(1, 0)),
        (0x10000, FACTORY.read_bytes()),
        (0x190000, diag_blob),
    )
    for offset, blob in pieces:
        if offset + len(blob) > len(image):
            sys.stderr.write(f"image does not fit at {offset:#x}\n")
            return 1
        image[offset : offset + len(blob)] = blob
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(image)
    print(f"wrote {OUT} ({len(image)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
