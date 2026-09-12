#!/usr/bin/env python3
"""Compile offline JSON metadata into a canonical 256-byte FXM1 .fxm sidecar.

Keys: title (required), publisher, developer, genre, year, min_players,
max_players, chips (array of chip names), msu1 (boolean). Omitted optional fields default
to empty/zero. Output should be named <full ROM filename>.fxm.
"""
import argparse
import binascii
import json
from pathlib import Path
import struct

CHIPS = {name: 1 << index for index, name in enumerate(
    ('SA1', 'SUPERFX', 'DSP', 'CX4', 'SDD1', 'SPC7110', 'ST010', 'ST011', 'ST018', 'OBC1'))}
TEXT = {'title': (32, 64), 'publisher': (96, 32), 'developer': (128, 32), 'genre': (160, 24)}


def compile_metadata(metadata):
    if not isinstance(metadata, dict):
        raise ValueError('Metadata must be a JSON object')
    unknown = metadata.keys() - (TEXT.keys() | {'year', 'min_players', 'max_players', 'chips', 'msu1'})
    if unknown:
        raise ValueError('Unknown metadata keys: ' + ', '.join(sorted(unknown)))
    record = bytearray(256)
    for key, (offset, size) in TEXT.items():
        value = metadata.get(key, '')
        if not isinstance(value, str) or any(not 32 <= ord(c) <= 126 for c in value):
            raise ValueError(f'{key} must contain printable ASCII only')
        if len(value) >= size or (key == 'title' and not value):
            raise ValueError(f'{key} must be {"1" if key == "title" else "0"}..{size - 1} characters')
        record[offset:offset + len(value)] = value.encode('ascii')
    numbers = [metadata.get(key, 0) for key in ('year', 'min_players', 'max_players')]
    if any(type(n) is not int for n in numbers):
        raise ValueError('Year and player counts must be integers')
    year, minimum, maximum = numbers
    if year != 0 and not 1900 <= year <= 2199:
        raise ValueError('Year must be zero or 1900..2199')
    if not ((minimum == maximum == 0) or (1 <= minimum <= maximum <= 8)):
        raise ValueError('Players must both be zero or 1 <= min_players <= max_players <= 8')
    chips = metadata.get('chips', [])
    if not isinstance(chips, list) or any(not isinstance(chip, str) or chip not in CHIPS for chip in chips):
        raise ValueError('chips must be an array of known chip names')
    if len(set(chips)) != len(chips):
        raise ValueError('Duplicate chips')
    msu1 = metadata.get('msu1', False)
    if type(msu1) is not bool:
        raise ValueError('msu1 must be a boolean')
    struct.pack_into('<HBBHH', record, 184, year, minimum, maximum,
                     sum(CHIPS[chip] for chip in chips), int(msu1))
    crc = binascii.crc_hqx(record[32:], 0xffff)
    struct.pack_into('<4sHHIHHHHI', record, 0, b'FXM1', 1, 256, 0, 224, 0, crc, crc ^ 65535, 0xffffffff)
    return bytes(record)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        metadata = json.loads(args.input.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
        result = compile_metadata(metadata)
        args.output.write_bytes(result)
    except (ValueError, OSError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
