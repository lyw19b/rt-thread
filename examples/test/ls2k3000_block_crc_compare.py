#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               compare read-only cross-OS sector CRCs
#
# Description:
#   Compare a captured Linux reference with ls2k3000_block_crc serial output.
#   Missing, duplicate or out-of-range sectors make the comparison fail.
#   A matching CRC is a checksum comparison, not a cryptographic byte proof.

import argparse
import base64
import json
from pathlib import Path
import re
import struct
import zlib


def compare(linux_text, rtos_text, phase):
    references = re.findall(
        r'SECTOR_REFERENCE ' + re.escape(phase) +
        r' (\d+) (\d+) ([0-9a-f]{8}) ([A-Za-z0-9+/=]+)', linux_text)
    if len(references) != 1:
        raise ValueError('Expected exactly one Linux reference for the selected phase')

    start_text, count_text, total, encoded = references[0]
    start, count = int(start_text), int(count_text)
    if not 0 < count <= 131072:
        raise ValueError('Invalid reference sector count')

    packed = zlib.decompress(base64.b64decode(encoded, validate=True))
    if len(packed) != count * 4:
        raise ValueError('Reference CRC vector length mismatch')
    expected = struct.unpack('<' + str(count) + 'I', packed)
    actual = {}

    for match in re.finditer(r'SECTORCRC (\d+)((?: [0-9a-f]{8}){1,16})(?=\r?\n|$)', rtos_text):
        sector = int(match.group(1))
        for offset, value in enumerate(match.group(2).split()):
            index = sector + offset
            if not start <= index < start + count or index in actual:
                raise ValueError(f'Duplicate or out-of-range sector: {index}')
            actual[index] = int(value, 16)

    footer = re.findall(
        r'SECTORCRC PASS device=(\S+) start=(\d+) sectors=(\d+) crc32=([0-9a-f]{8})',
        rtos_text)
    if len(footer) != 1 or int(footer[0][1]) != start or int(footer[0][2]) != count:
        raise ValueError('Missing or ambiguous successful RT-Thread range footer')
    missing = [index for index in range(start, start + count) if index not in actual]
    changed = [{'sector': index, 'linux': f'{expected[index - start]:08x}',
                'rtos': f'{actual[index]:08x}'}
               for index in range(start, start + count)
               if index in actual and actual[index] != expected[index - start]]
    return {'phase': phase, 'start': start, 'sectors': count, 'device': footer[0][0],
            'linux_crc32': total, 'rtos_crc32': footer[0][3],
            'missing': missing, 'changed': changed,
            'pass': not missing and not changed and total == footer[0][3]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('linux_log', type=Path)
    parser.add_argument('rtos_log', type=Path)
    parser.add_argument('--phase', default='unmounted', help='reference phase label to compare')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = compare(args.linux_log.read_text(errors='replace'),
                     args.rtos_log.read_text(errors='replace'), args.phase)
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    raise SystemExit(0 if result['pass'] else 1)


if __name__ == '__main__':
    main()
