#!/usr/bin/env python3
# Copyright (C) 2026 OpenLANPU Contributors
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-25     OpenLANPU Contributors    retain USB sanitizer and build matrix
#
# Description:
#   Run actual EHCI/BOT code under sanitizers and cross-build private BSP images.
# Notes:
#   QEMU smoke checks use USB-disabled images: Novae has no USB controller model.

"""Validate LS2K0300 USB software without modifying the checkout configuration."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'bsp/loongarch/ls2k0300/drivers'
COMMON = ROOT / 'components/drivers/usb/cherryusb/common'


def setting(config, name, value):
    config = re.sub(rf'^(?:CONFIG_{name}=.*|# CONFIG_{name} is not set)\n', '', config, flags=re.M)
    return config + f'CONFIG_{name}={value}\n'


def model(output, cc):
    binary = output / 'usb-model'
    command = [cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(DRIVER), '-I', str(COMMON),
               str(ROOT / 'examples/test/ls2k_usb_test.c'),
               str(DRIVER / 'ls2k_ehci.c'), str(DRIVER / 'ls2k_usb_bot.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    (output / 'model.log').write_text(result.stdout + result.stderr)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        raise RuntimeError((output / 'model.log').read_text())


def builds(args):
    sys.path.insert(0, str(ROOT.parents[2] / 'system/scripts'))
    from rtthread import build_environment, prepare_kernel

    bsp = prepare_kernel(args.output / 'cross')
    env = build_environment(args.toolchain, 'loongarch64-linux-musl-')
    base = (bsp / 'usb_defconfig').read_text()
    page = setting(setting(base, 'KMD_USE_DMW', 'n'), 'KMD_USE_PAGE', 'y')
    standard = setting(setting(base, 'RT_USING_SMART', 'n'), 'RT_USING_SMP', 'n')
    combined = setting((bsp / 'sd_network_defconfig').read_text(), 'BSP_USING_USB_HOST', 'y')
    combined = setting(setting(combined, 'KMD_USE_DMW', 'n'), 'KMD_USE_PAGE', 'y')
    configs = {
        'dmw-usb': base,
        'page-usb': page,
        'standard-usb': standard,
        'page-usb-sd0-gmac0': combined,
        'disabled-dmw': setting(base, 'BSP_USING_USB_HOST', 'n'),
        'disabled-page': setting(page, 'BSP_USING_USB_HOST', 'n'),
    }
    results = {}
    for name, config in configs.items():
        (bsp / '.config').write_text(config)
        log_path = args.output / f'{name}-build.log'
        with log_path.open('w') as log:
            for command in (['scons', '--pyconfig-silent'], ['scons', '-j8']):
                subprocess.run(command, cwd=bsp, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True)

        if re.search(r'warning:|error:', log_path.read_text()):
            raise RuntimeError(f'Compiler diagnostics: {log_path}')
        prefix = args.toolchain / 'loongarch64-linux-musl-'
        symbols = subprocess.check_output([str(prefix) + 'nm', '-n', str(bsp / 'rtthread.elf')], text=True)
        addresses = {line.split()[-1]: int(line.split()[0], 16)
                     for line in symbols.splitlines() if len(line.split()) == 3}
        if not name.startswith('disabled'):
            sections = subprocess.check_output([str(prefix) + 'readelf', '-lW', str(bsp / 'rtthread.elf')], text=True)
            assert '.usb_dma' not in sections, 'DMA must not be loaded via a cached alias'
            address = addresses['usb_storage']
            kernel_base = 0xffffffe000000000 if name.startswith('page') else 0x9000000000000000
            assert address % 4096 == 0
            assert addresses['__dma_start'] <= address < addresses['__dma_end']
            assert addresses['__dma_end'] - kernel_base < 0x10000000
            assert addresses['__usbh_class_info_end__'] > addresses['__usbh_class_info_start__']
            assert 'usb_osal_enter_critical_section' in addresses
            assert 'ls2k_usb_local_irq_enter' not in addresses, 'unused local-IRQ OSAL should be collected'
        else:
            assert 'usb_storage' not in addresses

        target = args.output / f'{name}.elf'
        shutil.copy2(bsp / 'rtthread.elf', target)
        shutil.copy2(bsp / '.config', args.output / f'{name}.config')
        results[name] = {'sha256': hashlib.sha256(target.read_bytes()).hexdigest(), 'passed': True}
        print(f'{name}: build / DMA / class table PASS', flush=True)
    return results


def smoke(args):
    import pexpect

    results = {}
    for name in ['disabled-dmw', 'disabled-page']:
        with (args.output / f'{name}-boot.log').open('w') as log:
            child = pexpect.spawn(str(args.qemu), ['-M', 'novae_2k0302', '-cpu', 'la464', '-m', '256M',
                                  '-nographic', '-kernel', str(args.output / f'{name}.elf')],
                                  encoding='utf-8', timeout=60)
            child.logfile_read = log
            try:
                child.expect(r'msh[^>]*>')
                child.sendline('kernel_map_test 8')
                child.expect('KERNEL MAP TEST: PASS')
                child.expect(r'msh[^>]*>')
                child.sendline('port_test')
                child.expect('KERNEL PORT TEST: PASS')
                results[name] = {'passed': True, 'usb_hardware': False}
            finally:
                child.close(force=True)
        print(f'{name}: QEMU kernel smoke PASS (USB disabled)', flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--qemu', type=Path, help='optional USB-disabled kernel smoke test')
    args = parser.parse_args()
    if args.build and not args.toolchain:
        parser.error('--build requires --toolchain')
    if args.qemu and not args.build:
        parser.error('--qemu requires --build')
    args.output = (args.output or Path(tempfile.mkdtemp(prefix='ls2k-usb-'))).resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    model(args.output, args.cc)
    results = {'model': {'passed': True, 'mib': 256}, 'hardware_verified': False}
    if args.build:
        results['builds'] = builds(args)
    if args.qemu:
        results['qemu'] = smoke(args)
    (args.output / 'result.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
