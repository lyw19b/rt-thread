#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               align file header with LS2K0300

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
    parser.add_argument('--mmc', action='store_true', help='Compile native SD/eMMC support')
    parser.add_argument('--mmc-clock', type=int, default=0, help='Verified MMC input clock in Hz')
    parser.add_argument('--emmc-base', type=lambda value: int(value, 0), default=0,
                        help='Verified ACPI eMMC MMIO base when PCI hides the host')
    parser.add_argument('--sd-base', type=lambda value: int(value, 0), default=0,
                        help='Verified ACPI SD MMIO base when PCI hides the host')
    parser.add_argument('--network', action='store_true', help='Compile GMAC2/lwIP support')
    parser.add_argument('--usb-mouse', action='store_true',
                        help='Compile native xHCI USB2 mouse input')
    parser.add_argument('--usb-camera', action='store_true',
                        help='Compile native xHCI USB2 ISO camera and mouse input')
    parser.add_argument('--peripherals', action='store_true',
                        help='Compile SPI, I2C, RTC, GPIO, PWM, HPET and watchdog')
    parser.add_argument('--test-source', type=Path, help='Inject a retained C acceptance test')
    parser.add_argument('--novaui', type=Path, help='Embed a native 1920x1080 Smart UI ELF')
    parser.add_argument('--gop-wide-writes', action='store_true',
                        help='Use aligned 64-bit pixel pairs on Explorer GOP memory')
    args = parser.parse_args()

    if args.novaui and args.profile == 'rtt':
        parser.error('--novaui requires Smart')

    if args.gop_wide_writes and not args.novaui:
        parser.error('--gop-wide-writes requires --novaui')

    if args.usb_camera and args.profile == 'rtt':
        parser.error('--usb-camera requires Smart')

    if (args.emmc_base or args.sd_base) and (not args.mmc or not args.mmc_clock):
        parser.error('Explicit MMC resources require --mmc and a verified --mmc-clock')

    for address in (args.emmc_base, args.sd_base):
        if address < 0 or address > 0xffffefff or address & 0xfff:
            parser.error('MMC resource addresses must be page-aligned 32-bit MMIO')

    if args.emmc_base and args.emmc_base == args.sd_base:
        parser.error('eMMC and SD must use distinct resources')

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

    if args.mmc:
        if not 0 <= args.mmc_clock <= 409200000:
            parser.error('--mmc-clock is outside the 10-bit identification divider range')

        with (board / '.config').open('a') as config:
            # Smart DFS partition/device registration uses about 6.5 KiB on
            # Explorer. Keep headroom for nested filesystem calls and IRQs.
            config.write('\nCONFIG_BSP_USING_LS2K3000_MMC=y\n'
                         'CONFIG_RT_MMCSD_STACK_SIZE=16384\n'
                         'CONFIG_RT_SDIO_STACK_SIZE=4096\n'
                         'CONFIG_BSP_LS2K3000_MMC_INPUT_HZ=' + str(args.mmc_clock) + '\n')

            if args.emmc_base or args.sd_base:
                config.write('CONFIG_BSP_LS2K3000_MMC_ACPI_STATIC=y\n'
                             f'CONFIG_BSP_LS2K3000_EMMC_BASE={args.emmc_base:#x}\n'
                             f'CONFIG_BSP_LS2K3000_SD_BASE={args.sd_base:#x}\n')

    if args.network:
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_RT_USING_LWIP=y\nCONFIG_RT_USING_LWIP212=y\n'
                         'CONFIG_BSP_USING_LS2K3000_GMAC=y\n'
                         'CONFIG_RT_LWIP_DHCP=y\nCONFIG_RT_LWIP_USING_PING=y\n'
                         'CONFIG_RT_LWIP_TCPTHREAD_STACKSIZE=4096\n'
                         'CONFIG_RT_LWIP_ETHTHREAD_STACKSIZE=4096\n'
                         'CONFIG_RT_USING_NETDEV=y\nCONFIG_RT_USING_SAL=y\n'
                         'CONFIG_SAL_USING_POSIX=y\n'
                         'CONFIG_RT_SYSTEM_WORKQUEUE_STACKSIZE=4096\n')

    if args.usb_mouse or args.usb_camera:
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_BSP_USING_USB_MOUSE=y\n')

    if args.usb_camera:
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_BSP_USING_USB_CAMERA=y\n')

    if args.peripherals:
        with (board / '.config').open('a') as config:
            config.write('\nCONFIG_RT_USING_SOFT_RTC=n\n')
            for name in ('SPI', 'I2C', 'RTC', 'GPIO', 'PWM', 'HPET', 'WDT'):
                config.write('\nCONFIG_BSP_USING_LS2K3000_' + name + '=y\n')

    env = os.environ.copy()
    env['RTT_ROOT'] = str(snapshot)
    env['RTT_EXEC_PATH'] = str(args.toolchain.resolve())
    env['RTT_CC_PREFIX'] = 'loongarch64-linux-musl-'
    env.pop('MKIMAGE', None)

    if args.usb_camera:
        camera_source = RTT.parents[1] / 'driver'
        camera_snapshot = output / 'driver'

        for directory in ('rtos/camera', 'include'):
            shutil.copytree(camera_source / directory, camera_snapshot / directory, ignore=ignore)

        env['LS2K3000_CAMERA_DIR'] = str(camera_snapshot / 'rtos/camera')

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
            if args.gop_wide_writes:
                config.write('\nCONFIG_BSP_NOVAUI_GOP_WRITE64=y\n')

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
                'nvme': args.nvme, 'mmc': args.mmc, 'mmc_clock': args.mmc_clock,
                'emmc_base': hex(args.emmc_base), 'sd_base': hex(args.sd_base),
                'network': args.network, 'peripherals': args.peripherals,
                'usb_mouse': args.usb_mouse or args.usb_camera,
                'usb_camera': args.usb_camera,
                'gop_wide_writes': args.gop_wide_writes,
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
