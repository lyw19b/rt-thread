#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
# SPDX-License-Identifier: Apache-2.0
#
# File: build_kernel.py
# Author: LoongsonLab

# Description:
#   Build an isolated LS2K3000 kernel snapshot and record exact image hashes.

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


BSP = Path(__file__).resolve().parents[1]
RTT = BSP.parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', choices=['rtt', 'smart', 'smart_smp'], default='rtt')
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--nvme', action='store_true', help='Enable read-only NVMe bring-up')
    parser.add_argument('--test-source', type=Path, help='Inject a retained C acceptance test')
    parser.add_argument('--novaui', type=Path, help='Embed a native 1920x1080 Smart UI ELF')
    args = parser.parse_args()

    if args.novaui and args.profile == 'rtt':
        parser.error('--novaui requires Smart')

    output = args.output.resolve()
    snapshot = output / 'rt-thread'

    if output == RTT or RTT in output.parents or snapshot.exists():
        parser.error('Use a fresh output directory outside RT-Thread')

    output.mkdir(parents=True, exist_ok=True)
    ignore = shutil.ignore_patterns('.git', '__pycache__', 'build', '.sconsign*',
                                    '*.elf', '*.bin', '*.map', 'compile_commands.json',
                                    '.config', '.config.old', 'rtconfig.h')

    for directory in ('src', 'include', 'components', 'libcpu', 'tools', 'examples',
                      'bsp/loongarch/common', 'bsp/loongarch/ls2k3000'):
        shutil.copytree(RTT / directory, snapshot / directory, ignore=ignore)

    shutil.copy2(RTT / 'Kconfig', snapshot / 'Kconfig')
    board = snapshot / 'bsp/loongarch/ls2k3000'
    shutil.copy2(board / 'config' / (args.profile + '_defconfig'), board / '.config')

    if args.test_source:
        shutil.copy2(args.test_source, board / 'applications/acceptance_test.c')

    if args.nvme:
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_BSP_USING_NVME=y\n')

    env = os.environ.copy()
    env['RTT_ROOT'] = str(snapshot)
    env['RTT_EXEC_PATH'] = str(args.toolchain.resolve())
    env['RTT_CC_PREFIX'] = 'loongarch64-linux-musl-'
    env.pop('MKIMAGE', None)

    if args.novaui:
        application = args.novaui.resolve()
        payload = application.read_bytes()

        if payload[:6] != b'\x7fELF\x02\x01' or len(payload) > 56 * 1024 * 1024:
            parser.error('UI must be a little-endian ELF64 within the 56 MiB budget')

        ui_source = RTT.parents[1] / 'display/novauiOS'
        ui_snapshot = output / 'novauiOS'

        for directory in ('platform/rtos', 'src/measure/cpu'):
            shutil.copytree(ui_source / directory, ui_snapshot / directory, ignore=ignore)

        env['NOVAUI_RTOS_DIR'] = str(ui_snapshot / 'platform/rtos')
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_BSP_USING_NOVAUI=y\n'
                         'CONFIG_BSP_NOVAUI_OPTIONAL_TOUCH=y\n'
                         'CONFIG_BSP_NOVAUI_SINGLE_BUFFER=y\n'
                         'CONFIG_BSP_USING_BOOT_SCREEN=n\n'
                         'CONFIG_RT_USING_DFS_ROMFS=y\n'
                         'CONFIG_RT_PAGECACHE_COUNT=32768\n'
                         'CONFIG_RT_PAGECACHE_ASPACE_COUNT=16384\n')

        with (board / 'rtconfig.py').open('a') as config:
            config.write("\nCFLAGS += ' -DLCD_WIDTH=1920 -DLCD_HEIGHT=1080'\n")

        # Keep both the source payload and its assembly wrapper in the snapshot.
        embedded = output / 'novauios.elf'
        embedded.write_bytes(payload)
        assembly = ('\n.section .rodata\n.balign 16\n.global novaui_image\n'
                    'novaui_image:\n.incbin ' + json.dumps(str(embedded)) + '\n.previous\n')
        (board / 'applications/novaui_image.c').write_text(
            '/* Generated NovaUIOS payload; not a source file. */\n'
            '__asm__(' + json.dumps(assembly) + ');\n'
            'const unsigned long novaui_image_size = ' + str(len(payload)) + ';\n')

    for name, command in (('configure', ['scons', '--pyconfig-silent']),
                          ('build', ['scons', '-j' + str(args.jobs)])):
        with (output / (name + '.log')).open('w') as log:
            result = subprocess.run(command, cwd=board, env=env,
                                    stdout=log, stderr=subprocess.STDOUT)

        if result.returncode:
            print((output / (name + '.log')).read_text()[-8000:])
            raise SystemExit(result.returncode)

    manifest = {'status': 'BUILD_PASS_NOT_BOARD_TESTED', 'profile': args.profile,
                'nvme': args.nvme,
                'novaui_sha256': (hashlib.sha256(args.novaui.read_bytes()).hexdigest()
                                  if args.novaui else None),
                'test_source_sha256': (hashlib.sha256(args.test_source.read_bytes()).hexdigest()
                                       if args.test_source else None),
                'kernel': str(board / 'rtthread.bin'), 'hashes': {}}

    for name in ('rtthread.bin', 'rtthread.elf', 'rtconfig.h', '.config'):
        manifest['hashes'][name] = hashlib.sha256((board / name).read_bytes()).hexdigest()

    (output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
