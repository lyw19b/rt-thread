#!/usr/bin/env python3
# Copyright (C) 2026 OpenLANPU Contributors
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-24     OpenLANPU Contributors    retain SDIO sanitizer regression runner
#

# Description:
#   Compiles and runs the actual SDIO engine against a fault-injecting MMIO model.
#
# Notes:
#   Artifacts are placed outside the repository. This does not emulate SD silicon.
#

"""Run LS2K0300 SDIO host regressions with address and undefined-behavior sanitizers."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'bsp/loongarch/ls2k0300/drivers'


def cross_build(output, toolchain):
    # Use the project's established isolated build path; never rewrite the
    # checkout's .config/rtconfig.h or include unrelated local build artifacts.
    sys.path.insert(0, str(ROOT.parents[2] / 'system/scripts'))
    from rtthread import build_environment, prepare_kernel

    build = output / 'cross'
    bsp = prepare_kernel(build)
    env = build_environment(toolchain, 'loongarch64-linux-musl-')
    base = (bsp / 'sd_network_defconfig').read_text()
    variants = {
        'disabled': (bsp / 'smart_defconfig').read_text(),
        'standard-up': base + '\nCONFIG_RT_USING_SMART=n\nCONFIG_RT_USING_SMP=n\n',
        'sdio1': base + '\nCONFIG_BSP_SDIO_PORT=1\n',
        'smart-sdio0-gmac0': base,
    }

    for name, config in variants.items():
        # Resolve repeated keys before Kconfig so overrides are intentional.
        entries = {}
        for line in config.splitlines():
            match = re.match(r'(CONFIG_\w+)=', line)
            if match:
                entries[match.group(1)] = line
        (bsp / '.config').write_text('\n'.join(entries.values()) + '\n')
        log_path = output / f'{name}-build.log'
        with log_path.open('w') as log:
            for command in (['scons', '--pyconfig-silent'], ['scons', '-j8']):
                subprocess.run(command, cwd=bsp, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True)

        header = (bsp / 'rtconfig.h').read_text()
        assert ('#define BSP_USING_SDIO\n' in header) == (name != 'disabled')
        if name != 'disabled':
            assert '#define BSP_USING_GMAC0\n' in header
            assert '#define BSP_USING_GMAC1\n' not in header
            expected = 1 if name == 'sdio1' else 0
            assert f'#define BSP_SDIO_PORT {expected}\n' in header
            prefix = toolchain / 'loongarch64-linux-musl-'
            sections = subprocess.check_output([str(prefix) + 'readelf', '-lW',
                                                str(bsp / 'rtthread.elf')], text=True)
            assert '.sdio_dma' not in sections, 'DMA must not be initialized via cached ELF alias'
            symbols = subprocess.check_output([str(prefix) + 'nm', '-n',
                                               str(bsp / 'rtthread.elf')], text=True)
            addresses = {line.split()[-1]: int(line.split()[0], 16)
                         for line in symbols.splitlines() if len(line.split()) == 3}
            assert addresses['__bss_end'] <= addresses['storage'] < addresses['_end']
            assert addresses['storage'] % 64 == 0
            assert addresses['_end'] & 0x0000ffffffffffff < 0x10000000

        assert ('#define RT_USING_SMART\n' in header) == (name != 'standard-up')
        if re.search(r'warning:|error:', log_path.read_text()):
            raise RuntimeError(f'Compiler diagnostics: {log_path}')
        shutil.copy2(bsp / 'rtthread.elf', output / f'{name}.elf')
        print(f'{name}: cross-build and DMA placement PASS', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--build', action='store_true', help='also cross-build four isolated BSP variants')
    parser.add_argument('--toolchain', type=Path, help='LoongArch musl compiler bin directory')
    args = parser.parse_args()
    if args.build and not args.toolchain:
        parser.error('--build requires --toolchain')
    output = (args.output or Path(tempfile.mkdtemp(prefix='ls2k-sdio-'))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output.resolve() / 'sdio-test'
    command = [args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(DRIVER), str(ROOT / 'examples/test/ls2k_sdio_test.c'),
               str(DRIVER / 'ls2k_sdio.c'), '-o', str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)

    # Compile the actual pool declarations. Mailboxes count pointer-sized slots,
    # not bytes: uint32_t[4] overflows on the third 64-bit host notification.
    source = (ROOT / 'components/drivers/sdio/dev_mmcsd_core.c').read_text()
    pools = re.findall(r'^static rt_\w+ mmcsd_\w+_mb_pool\[4\];$', source, re.M)
    assert len(pools) == 2
    mailbox = output / 'mailbox-test.c'
    mailbox.write_text('''#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t rt_uint32_t;
typedef uintptr_t rt_ubase_t;
''' + '\n'.join(pools) + '''
int main(void)
{
    uintptr_t messages[4] = {0x9000000080010000UL, 0x9000000080020000UL,
                             0x9000000080030000UL, 0x9000000080040000UL};
    assert(sizeof(uintptr_t) == 8);
    assert(sizeof(mmcsd_detect_mb_pool) == sizeof(messages));
    assert(sizeof(mmcsd_hotpluge_mb_pool) == sizeof(messages));
    memcpy(mmcsd_detect_mb_pool, messages, sizeof(messages));
    memcpy(mmcsd_hotpluge_mb_pool, messages, sizeof(messages));
    assert(!memcmp(mmcsd_detect_mb_pool, messages, sizeof(messages)));
    assert(!memcmp(mmcsd_hotpluge_mb_pool, messages, sizeof(messages)));
    return 0;
}
''')
    subprocess.run([args.cc, '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(mailbox), '-o', str(output / 'mailbox-test')], check=True)
    subprocess.run([str((output / 'mailbox-test').resolve())], check=True)
    print('MMC/SD 64-bit notification queues: PASS')
    print(f'Executable: {binary}')
    if args.build:
        cross_build(output, args.toolchain.resolve())


if __name__ == '__main__':
    main()
