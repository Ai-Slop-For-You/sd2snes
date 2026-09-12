#!/usr/bin/env python3
"""Check Mk.III firmware integrity, flash layout, vectors and embedded mini core."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("elf", type=Path)
    parser.add_argument("mini", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    fw, mini = args.firmware.read_bytes(), args.mini.read_bytes()
    magic, version, size, crc, inverse = struct.unpack_from("<4s4I", fw)
    require(magic == b"SNS3", "Wrong board signature")
    require(size == len(fw) - 256 and 0 < len(fw) <= 0x34000, "Invalid firmware size")
    require(version == zlib.crc32(args.version.encode()), "Wrong firmware version")
    require(zlib.crc32(fw[256:]) == crc and crc ^ inverse == 0xffffffff, "Firmware CRC mismatch")
    require(fw[20:256] == b"\xff" * 236, "Invalid header padding")
    with tempfile.TemporaryDirectory() as temporary:
        image = Path(temporary) / "firmware.bin"
        subprocess.check_call(["arm-none-eabi-objcopy", "--gap-fill", "0xff",
                               "-O", "binary", str(args.elf), str(image)])
        require(image.read_bytes() == fw, "Firmware differs from linked ELF")
    symbols = {}
    for line in subprocess.check_output(["arm-none-eabi-nm", "-S", str(args.elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) in (3, 4):
            symbols[fields[-1]] = (int(fields[0], 16), int(fields[1], 16) if len(fields) == 4 else None)
    address, length = symbols["cfgware"]
    require(length == len(mini), "Wrong embedded bitstream size")
    require(fw[address - 0xc000:address - 0xc000 + length] == mini, "Embedded bitstream differs")
    require(symbols["__data_load_end"][0] == 0xc000 + len(fw), "ELF flash end differs from firmware")
    stack, reset = struct.unpack_from("<II", fw, 256)
    require(stack == symbols["__stack"][0] == 0x10004000, "Invalid initial stack")
    require(reset == symbols["_start"][0] | 1, "Invalid Thumb reset vector")
    remaining = stack - symbols["__heap_start"][0]
    require(remaining > 0, "No main RAM remaining for heap/stack")
    require(symbols["__ahbram_end__"][0] <= 0x20080000, "AHB RAM overflow")
    print(f"Mk.III firmware valid: {len(fw)} bytes, {0x34000-len(fw)} flash bytes free")
    print(f"Main RAM remaining for shared heap/stack: {remaining} bytes (static check, not a runtime bound)")
    print("SHA-256 " + hashlib.sha256(fw).hexdigest())


if __name__ == "__main__":
    main()
