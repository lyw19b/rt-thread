#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               retain read-only Linux sector reference
#
# Description:
#   Capture a bounded 512-byte-sector range for ls2k3000_block_crc_compare.py.
#   Opens only for reading; never mounts, unmounts, syncs or writes the device.
#   The caller must establish that the selected range is stable. The phase
#   label records that external condition and does not establish it itself.

import argparse
import base64
import hashlib
from pathlib import Path
import struct
import zlib


def main():
    parser = argparse.ArgumentParser(description='Capture a read-only Linux sector CRC reference')
    parser.add_argument('device', type=Path)
    parser.add_argument('start', type=int)
    parser.add_argument('sectors', type=int)
    parser.add_argument('--phase', default='captured')
    parser.add_argument('--dump-sector', type=int, action='append', default=[])
    args = parser.parse_args()
    if args.start < 0 or not 0 < args.sectors <= 131072:
        parser.error('Require start >= 0 and 1..131072 sectors')
    if not args.phase or any(ch.isspace() for ch in args.phase):
        parser.error('Phase must be one nonempty word')
    if len(args.dump_sector) > 16 or any(
            not args.start <= sector < args.start + args.sectors for sector in args.dump_sector):
        parser.error('At most sixteen metadata sectors, all within the captured range')

    with args.device.open('rb', buffering=0) as device:
        device.seek(args.start * 512)
        data = device.read(args.sectors * 512)
    if len(data) != args.sectors * 512:
        raise RuntimeError('Short read; no complete reference produced')

    checks = [zlib.crc32(data[index:index + 512]) for index in range(0, len(data), 512)]
    packed = struct.pack('<' + str(len(checks)) + 'I', *checks)
    encoded = base64.b64encode(zlib.compress(packed)).decode('ascii')
    print('SECTOR_REFERENCE', args.phase, args.start, args.sectors,
          f'{zlib.crc32(data):08x}', encoded, flush=True)
    print('SECTOR_RANGE_SHA256', args.phase, hashlib.sha256(data).hexdigest(), flush=True)

    for sector in args.dump_sector:
        offset = (sector - args.start) * 512
        print('SECTOR_BYTES', args.phase, sector, data[offset:offset + 512].hex(), flush=True)


if __name__ == '__main__':
    main()
