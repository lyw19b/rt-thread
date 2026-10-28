#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               retain Explorer network payload tests

# Description:
#   Exercise bounded UDP/TCP endpoints while NovaUIOS runs on the real board.
#   Requires pyserial and the retained ls2k_gmac_echo/ls2k_tcp_echo commands.

import argparse
import json
from pathlib import Path
import re
import socket
import time

import serial
from ls2k_gmac_test import payload


def verify_udp(peer, window):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.connect((peer, 9000))
        connection.settimeout(0.2)

        for first in range(0, 10000, window):
            pending = set(range(first, min(first + window, 10000)))

            for sequence in sorted(pending):
                connection.send(payload(sequence))

            end = time.monotonic() + 2

            while pending and time.monotonic() < end:
                try:
                    data = connection.recv(2048)
                except socket.timeout:
                    continue

                sequence = int.from_bytes(data[:8], 'big')

                if sequence not in pending or data != payload(sequence):
                    raise RuntimeError(f'Corrupt/duplicate UDP sequence {sequence}')

                pending.remove(sequence)

            if pending:
                raise RuntimeError(f'Missing UDP sequences: {sorted(pending)}')

            if first % 1000 == 0:
                print(f'UDP progress={first + window}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Stable physical serial path')
    parser.add_argument('--peer', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--window', type=int, choices=range(1, 17), default=1,
                        help='Outstanding UDP datagrams; default matches the one-slot receive mailbox')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    result = {'status': 'FAIL', 'peer': args.peer, 'udp_window': args.window}

    with serial.Serial(args.port, 115200, timeout=0.2, exclusive=True) as port, \
            (args.output / 'serial.log').open('wb') as log:
        def receive(marker=b'msh />', timeout=10):
            data = b''
            end = time.monotonic() + timeout

            while marker not in data and time.monotonic() < end:
                chunk = port.read(4096)
                log.write(chunk)
                log.flush()
                data += chunk

            text = data.decode(errors='replace')
            print(text, flush=True)

            if marker not in data:
                raise RuntimeError('Serial timeout: ' + repr(marker))

            return text

        def command(text):
            port.write(text.encode() + b'\r')
            return receive()

        def counters(text):
            line = re.search(r'e0 phy=.*', text)

            if not line:
                raise RuntimeError('Missing Ethernet status')

            return {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line[0])}

        try:
            command('')
            result['before'] = counters(command('ls2k3000_net_status'))
            port.write(b'gmac_echo 45\r')
            receive(b'45 seconds')
            verify_udp(args.peer, args.window)
            result['udp'] = receive(timeout=55)

            if 'packets=10000 ' not in result['udp'] or 'result=0' not in result['udp']:
                raise RuntimeError('UDP endpoint count/result mismatch')

            port.write(b'gmac_tcp_echo 120\r')
            receive(b'deadline=120 seconds')
            start = time.monotonic()
            total = 16 * 1024 * 1024
            block = bytes((index * 37 + 11) & 255 for index in range(16384))

            with socket.create_connection((args.peer, 9001), timeout=5) as connection:
                connection.settimeout(5)

                for offset in range(0, total, len(block)):
                    # Change each chunk's prefix to detect stale or reordered data.
                    payload = offset.to_bytes(8, 'little') + block[8:]
                    connection.sendall(payload)
                    actual = b''

                    while len(actual) < len(payload):
                        chunk = connection.recv(len(payload) - len(actual))

                        if not chunk:
                            raise RuntimeError('Truncated TCP stream')

                        actual += chunk

                    if actual != payload:
                        raise RuntimeError(f'TCP payload mismatch at {offset}')

            result['tcp_seconds'] = time.monotonic() - start
            result['tcp'] = receive()

            if f'bytes={total} result=0' not in result['tcp']:
                raise RuntimeError('TCP endpoint count/result mismatch')

            result['after'] = counters(command('ls2k3000_net_status'))
            result['ui'] = command('novaui_status')

            for name in ('txerr', 'rxerr', 'drop', 'buserr', 'fault'):
                if result['after'][name] != result['before'][name]:
                    raise RuntimeError('Ethernet error counter changed: ' + name)

            if 'active=1' not in result['ui'] or 'errors=0' not in result['ui']:
                raise RuntimeError('NovaUIOS health check failed')

            result['status'] = 'PASS'
        except Exception as error:
            result['error'] = str(error)
            raise
        finally:
            (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')

    print('PASS: 10000 verified UDP echoes and 16 MiB verified TCP echo', flush=True)


if __name__ == '__main__':
    main()
