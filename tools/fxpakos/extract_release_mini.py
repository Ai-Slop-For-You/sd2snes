#!/usr/bin/env python3
"""Recover the unchanged Mk.III mini core from the official 1.11.0 firmware.

This is an explicit alternative to local Quartus synthesis, not a synthesized
asset. Pin the complete archive, firmware, code references and extracted bytes.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import urllib.request
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = "31dca4678ee8acbef8da1d05aeda35061eeccbfd"
URL = "https://sd2snes.de/files/sd2snes_firmware_v1.11.0.zip"
ARCHIVE_SHA = "8a56c4a23be13eed51f11e82525f8d812a8fe23567ca2708de507ad51c62fb64"
FIRMWARE_SHA = "8393cd381d71bc30b363802c718b39b80987171a09a6b83a27d5e000305ba5ab"
MINI_SHA = "9ae79c3028391063338d42ae16b19acf48d0d80858939b015ef6481f55cbefe9"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def checked(data, digest, label):
    require(hashlib.sha256(data).hexdigest() == digest, label + " SHA-256 mismatch")
    return data


def check_sources():
    def git(*args):
        return subprocess.check_output(["git", "-C", str(ROOT), *args])

    prefix = "verilog/sd2snes_mini/"
    files = git("ls-tree", "-r", "--name-only", UPSTREAM, "--", prefix).decode().splitlines()
    require(bool(files), "Missing pinned upstream tree; fetch upstream tag v1.11.0")
    for name in files:
        if name == prefix + "Makefile":
            continue  # Dependency tracking changed; no FPGA design content.
        original = git("show", UPSTREAM + ":" + name)
        if name == prefix + "main.qsf":
            # This nonexistent, unused source entry was removed during Phase 2.
            original = original.replace(b"set_global_assignment -name VERILOG_FILE data.v\n", b"")
        require((ROOT / name).read_bytes() == original,
                "Mini core differs from pinned release: " + name + "; use Quartus synthesis")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ROOT / ".build/upstream/sd2snes_firmware_v1.11.0.zip")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/upstream/fpga_mini.bi3")
    args = parser.parse_args()
    check_sources()
    if not args.archive.exists():
        args.archive.parent.mkdir(parents=True, exist_ok=True)
        data = checked(urllib.request.urlopen(URL, timeout=45).read(), ARCHIVE_SHA, "archive")
        args.archive.write_bytes(data)
    checked(args.archive.read_bytes(), ARCHIVE_SHA, "archive")
    with zipfile.ZipFile(args.archive) as archive:
        fw = checked(archive.read("sd2snes/firmware.im3"), FIRMWARE_SHA, "firmware")
    magic, _, size, crc, inverse = struct.unpack_from("<4s4I", fw)
    require(magic == b"SNS3" and size == len(fw) - 256, "Invalid release firmware header")
    require(zlib.crc32(fw[256:]) == crc and crc ^ inverse == 0xffffffff, "Invalid release firmware CRC")
    # Thumb disassembly: 0x13696 loads r1=0xd5e2; 0x1369a loads r0 from
    # literal 0x137dc; 0x1369c calls rle_mem_init. The next call prints size.
    # Firmware flash base is 0xc000, so cfgware at 0x20b08 is file offset 0x14b08.
    require(fw[0x7696:0x76aa].hex() == "4df2e251504806f044fa4df2e2514e4806f0b7f9", "Unexpected cfgware call site")
    require(struct.unpack_from("<I", fw, 0x77dc)[0] == 0x20b08, "Unexpected cfgware address")
    mini = checked(fw[0x14b08:0x14b08 + 0xd5e2], MINI_SHA, "mini core")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(mini)
    print(f"Verified official v1.11.0 mini core: {len(mini)} bytes, SHA-256 {MINI_SHA}")
    print("Source: " + URL + "; this asset was NOT synthesized locally.")


if __name__ == "__main__":
    main()
