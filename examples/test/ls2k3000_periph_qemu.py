#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               align file header with LS2K0300

# Description:
#   Exercise read-only RT-Thread peripheral APIs on the existing LS2K3000 model.

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import select
import shutil
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--qemu-img', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kernel-cpus', type=int, choices=(1, 8), default=1)
    parser.add_argument('--stress', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    esp = output / 'esp/EFI/BOOT'
    esp.mkdir(parents=True)
    shutil.copyfile(args.image, esp / 'BOOTLOONGARCH64.EFI')
    disk = output / 'boot.img'
    subprocess.run([args.qemu_img, 'convert', '-f', 'vvfat', '-O', 'raw',
                    'fat:' + str(output / 'esp'), str(disk)], check=True)
    command = [str(args.qemu.resolve()), '-M', 'ls2k3000', '-m', '8G', '-smp', '8',
               '-bios', str(args.firmware.resolve()),
               '-drive', 'if=none,id=boot,format=raw,snapshot=on,file=' + str(disk),
               '-device', 'ide-hd,bus=sata.0,drive=boot', '-display', 'none',
               '-serial', 'stdio', '-monitor', 'none', '-nic', 'none']
    original = digest(args.firmware)
    result = {'status': 'FAIL', 'image_sha256': digest(args.image),
              'firmware_sha256': original, 'command': command,
              'scope': 'Existing model only; no physical-board validation'}
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    log = bytearray()
    sent = False
    stress_sent = False
    deadline = time.monotonic() + 360

    try:
        while time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.25)[0]:
                chunk = os.read(process.stdout.fileno(), 65536)

                if not chunk:
                    break

                log.extend(chunk)
                (output / 'serial.log').write_bytes(log)

            if b'Kernel arena unavailable; firmware memory preserved.' in log:
                result['failure'] = 'UEFI rejected the fixed 224 MiB arena before kernel entry'
                break

            if not sent and b'msh />' in log:
                process.stdin.write(b'ls2k3000_periph_test\r')
                process.stdin.flush()
                sent = True

            if not stress_sent and re.search(rb'PERIPH READ CHECKS COMPLETE passed=\d+ unavailable=\d+\r?\n', log):
                required = [b'PERIPH RTC PASS', b'PERIPH SPI0 PASS JEDEC=ef4017',
                            b'PERIPH SPI1 PASS JEDEC=ef4017', b'PERIPH I2C0 PASS',
                            b'PERIPH invalid address PASS', b'passed=5 unavailable=0']
                required.append(('RT-Thread ready: firmware services exited, CPUs=' +
                                 str(args.kernel_cpus)).encode())
                if args.kernel_cpus == 8:
                    required.append(b'SMP: 8 CPUs online (mask 0xff)')
                result['checks'] = {text.decode(): text in log for text in required}
                if args.stress and all(result['checks'].values()):
                    process.stdin.write(b'ls2k3000_periph_stress\r')
                    process.stdin.flush()
                    stress_sent = True
                else:
                    result['status'] = 'PASS' if all(result['checks'].values()) else 'FAIL'
                    break

            if stress_sent and re.search(rb'PERIPH STRESS (?:PASS|FAIL) workers=\d+ rounds=\d+ errors=\d+\r?\n', log):
                expected = b'PERIPH STRESS PASS workers=4 rounds=128 errors=0'
                result['checks'][expected.decode()] = expected in log
                result['status'] = 'PASS' if all(result['checks'].values()) else 'FAIL'
                break
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        result['firmware_unchanged'] = digest(args.firmware) == original
        if not result['firmware_unchanged']:
            result['status'] = 'FAIL'
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')

    print(json.dumps(result, indent=2))
    raise SystemExit(result['status'] != 'PASS')


if __name__ == '__main__':
    main()
