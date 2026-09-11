#!/usr/bin/env python3
"""Pack compile_art output into a fixed 4544-byte .fxc ROM sidecar (FXC1 v1)."""
import argparse
import binascii
import struct
from pathlib import Path
HEADER_SIZE=32
RECORD_SIZE=4544

def pack(prefix: Path) -> bytes:
    tiles=prefix.with_suffix('.4bpp').read_bytes()
    palette=prefix.with_suffix('.pal').read_bytes()
    tilemap=prefix.with_suffix('.map').read_bytes()
    if len(tiles)!=4480 or len(palette)!=32:
        raise ValueError('cover must be 80x112, 4bpp with a 16-entry palette')
    if tilemap!=struct.pack('<140H',*range(140)):
        raise ValueError('v1 requires implicit row-major tiles: base/palette/priority zero')
    if any(c&0x8000 for c in struct.unpack('<16H',palette)) or palette[:2]!=b'\0\0':
        raise ValueError('palette must be BGR555 with transparent entry zero')
    payload=tiles+palette
    crc=binascii.crc_hqx(payload,0xffff)
    header=struct.pack('<4sHHI4BHHHHII',b'FXC1',1,RECORD_SIZE,0,80,112,1,0,len(payload),0,crc,crc^0xffff,0xffffffff,0)
    return header+payload

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('prefix',type=Path)
    ap.add_argument('output',type=Path,help='ROM path plus .fxc, e.g. Game.sfc.fxc')
    a=ap.parse_args();data=pack(a.prefix);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
    print(f'FXC1: {len(data)} bytes -> {a.output}')
if __name__=='__main__':main()
