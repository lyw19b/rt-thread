#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               align file header with LS2K0300

# Description:
#   Test native modeled peripherals with an explicit synthetic boot contract.
#   This bypasses UEFI allocation and is never a real-board loader.

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import select
import subprocess
import time


ENTRY = r'''
.global _start
.section .text
_start:
    csrrd $t0, 0x20
    andi $t0, $t0, 0x3ff
    bnez $t0, secondary
    li.d $t0, 0xfe00001010
    li.w $t1, 0x40000004
    st.w $t1, $t0, 0
    st.w $zero, $t0, 4
    li.w $t1, 2
    st.w $t1, $t0, -12
    li.d $t0, 0xfe0000b010
    li.w $t1, 0x40080004
    st.w $t1, $t0, 0
    st.w $zero, $t0, 4
    li.w $t1, 2
    st.w $t1, $t0, -12
    dbar 0
    li.d $t0, 0x40050140
    li.w $t1, 0x2900
    st.w $t1, $t0, 0
    dbar 0
    li.d $a0, 0x4c53334b55454649
    li.d $a1, 0x0dff0000
    move $a2, $zero
    li.d $t0, 0x02000000
    jr $t0
secondary:
    li.w $t1, 0x1020
1:
    iocsrrd.d $t0, $t1
    beqz $t0, 1b
    dbar 0
    jr $t0
'''

HANDOFF = r'''
#include <stdio.h>
#include <stdlib.h>
#include "boot_info.h"
int main(int argc, char **argv)
{
    static struct ls2k3000_boot_info info;
    if (argc != 2)
    {
        return 1;
    }
    info.magic = LS2K3000_BOOT_MAGIC;
    info.version = LS2K3000_BOOT_VERSION;
    info.bytes = sizeof(info);
    info.kernel_base = LS2K3000_KERNEL_PHYS;
    info.reserved_bytes = LS2K3000_RESERVED_BYTES;
    info.kernel_bytes = strtoull(argv[1], NULL, 10);
    info.memory_count = 1;
    info.memory[0].base = LS2K3000_KERNEL_PHYS;
    info.memory[0].pages = LS2K3000_RESERVED_BYTES / 4096;
    info.memory[0].type = 2;
    info.pci_count = 2;
    info.pci[0].devfn = 2 << 3;
    info.pci[0].id = 0x7a420014;
    info.pci[0].class_revision = 0x08800000;
    info.pci[0].bar[0].base = 0x40000000;
    info.pci[0].bar[0].bytes = 0x80000;
    info.pci[1].devfn = 22 << 3;
    info.pci[1].id = 0x7a1b0014;
    info.pci[1].class_revision = 0x0c800000;
    info.pci[1].bar[0].base = 0x40080000;
    info.pci[1].bar[0].bytes = 0x1000;
    info.checksum = ls2k3000_boot_checksum(&info);
    if (ls2k3000_boot_validate(&info))
    {
        return 2;
    }
    return fwrite(&info, sizeof(info), 1, stdout) != 1;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpus', type=int, choices=(1, 8), default=8)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    bsp = Path(__file__).resolve().parents[2] / 'bsp/loongarch/ls2k3000'
    (output / 'entry.S').write_text(ENTRY)
    (output / 'handoff.c').write_text(HANDOFF)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I' + str(bsp / 'boot'),
                    str(output / 'handoff.c'), str(bsp / 'boot/boot_contract.c'),
                    '-o', str(output / 'handoff')], check=True)
    with (output / 'handoff.bin').open('wb') as stream:
        subprocess.run([str(output / 'handoff'), str(args.kernel.stat().st_size)],
                       stdout=stream, check=True)
    prefix = str(args.toolchain.resolve() / 'loongarch64-linux-musl-')
    subprocess.run([prefix + 'gcc', '-nostdlib', '-static', '-no-pie',
                    '-Wl,--build-id=none', '-Wl,-Ttext=0x1000000', '-Wl,-e,_start',
                    str(output / 'entry.S'), '-o', str(output / 'entry.elf')], check=True)
    # The model requires an 8 MiB SPI backing image; this is disposable blank NOR.
    scratch = output / 'scratch-spi.bin'
    scratch.write_bytes(b'\xff' * (8 * 1024 * 1024))
    original = hashlib.sha256(scratch.read_bytes()).hexdigest()
    command = [str(args.qemu.resolve()), '-M', 'ls2k3000', '-m', '8G',
               '-smp', str(args.cpus), '-bios', str(scratch),
               '-device', 'loader,file=' + str(output / 'entry.elf') + ',cpu-num=0',
               '-device', 'loader,file=' + str(args.kernel.resolve()) + ',addr=0x02000000',
               '-device', 'loader,file=' + str(output / 'handoff.bin') + ',addr=0x0dff0000',
               '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-nic', 'none']
    for cpu in range(1, args.cpus):
        command += ['-device', 'loader,addr=0x1000000,cpu-num=' + str(cpu)]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    log = bytearray()
    sent = False
    stress = False
    passed = False
    deadline = time.monotonic() + 60
    try:
        while time.monotonic() < deadline:
            if select.select([process.stdout], [], [], 0.1)[0]:
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    break
                log.extend(chunk)
                (output / 'serial.log').write_bytes(log)
            if not sent and b'msh />' in log:
                process.stdin.write(b'ls2k3000_periph_test\r')
                process.stdin.flush()
                sent = True
            if not stress and re.search(rb'PERIPH READ CHECKS COMPLETE passed=\d+ unavailable=\d+\r?\n', log):
                if b'passed=5 unavailable=0' not in log:
                    break
                process.stdin.write(b'ls2k3000_periph_stress\r')
                process.stdin.flush()
                stress = True
            if re.search(rb'PERIPH STRESS (?:PASS|FAIL) workers=\d+ rounds=\d+ errors=\d+\r?\n', log):
                passed = b'PERIPH STRESS PASS workers=4 rounds=128 errors=0' in log
                if args.cpus == 8:
                    passed = passed and b'SMP: 8 CPUs online (mask 0xff)' in log
                break
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    result = {'passed': passed, 'cpus': args.cpus, 'command': command,
              'scope': 'Native peripheral model with synthetic resources; NO UEFI or physical board',
              'kernel_sha256': hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
              'scratch_unchanged': hashlib.sha256(scratch.read_bytes()).hexdigest() == original}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(log.decode(errors='replace'))
    print(json.dumps(result, indent=2))
    raise SystemExit(not (passed and result['scratch_unchanged']))


if __name__ == '__main__':
    main()
