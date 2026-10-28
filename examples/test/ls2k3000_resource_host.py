#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               test Explorer memory and APB resources

# Description:
#   Exercise production resource selection with mocked PCI configuration reads.
#   Check the fixed arena against the Explorer low-memory reservation boundary.

from pathlib import Path
import subprocess
import tempfile


STUB = r'''
#include <stdint.h>
#include <stddef.h>
#include "boot_info.h"
#define LS2K3000_BOARD_H
#define BSP_USING_LS2K3000_PERIPHERALS
#define RT_EOK 0
#define RT_EINVAL 1
#define RT_ETIMEOUT 2
#define RT_EBUSY 3
#define RT_ENOSYS 4
#define RT_EIO 5
#define LOONGARCH_PHYS_UNCACHED(value) ((uintptr_t)(value))
#define LOONGARCH_CPUCFG4 4
#define LOONGARCH_CPUCFG5 5
typedef uintptr_t rt_ubase_t;
typedef int rt_err_t;
extern const struct ls2k3000_boot_info *ls2k3000_boot;
static inline void rt_hw_dmb(void) {}
static inline uint64_t drdtime(void) { return 100; }
static inline uint32_t read_cpucfg(unsigned int n)
{
    return n == 4 ? 100000000 : 0x10001;
}
int ls2k3000_pci_map(const struct ls2k3000_pci_device *, unsigned int,
                   unsigned int, rt_ubase_t *);
'''

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "periph_resource.h"
static struct ls2k3000_boot_info boot;
const struct ls2k3000_boot_info *ls2k3000_boot = &boot;
static unsigned int pci_command;
static int bad_bar;

int ls2k3000_pci_map(const struct ls2k3000_pci_device *dev, unsigned int bar,
                   unsigned int bytes, rt_ubase_t *base)
{
    (void)dev;
    assert(bar == 0 && bytes <= 0x80000);
    *base = 0x40000000;
    return bad_bar;
}

int ls2k3000_pci_address(unsigned int bus, unsigned int devfn,
                       unsigned int offset, uint64_t *address)
{
    (void)bus;
    (void)devfn;
    assert(offset == 4);
    *address = (uintptr_t)&pci_command;
    return 0;
}

int main(void)
{
    struct ls_periph_io io;

    assert(LS2K3000_KERNEL_PHYS + LS2K3000_RESERVED_BYTES <= 0x0ee00000);
    assert(LS2K3000_HANDOFF_PHYS + LS2K3000_HANDOFF_BYTES ==
           LS2K3000_KERNEL_PHYS + LS2K3000_RESERVED_BYTES);
    assert(LS2K3000_DMA_PHYS + LS2K3000_DMA_BYTES < LS2K3000_HANDOFF_PHYS);
    assert(ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &io) == -RT_ENOSYS);
    boot.reset_address = 0x10150030;
    boot.reset_bits = 32;
    boot.reset_value = 1;
    assert(ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &io) == 0);
    assert((uintptr_t)io.context == 0x10150100);
    assert(ls2k3000_periph_map(0x7a1b, 0, 16, &io) == -RT_ENOSYS);
    boot.reset_bits = 8;
    assert(ls2k3000_periph_map(0x7a42, 0, 8, &io) == -RT_ENOSYS);
    boot.reset_bits = 32;
    boot.reset_value = 2;
    assert(ls2k3000_periph_map(0x7a42, 0, 8, &io) == -RT_ENOSYS);
    boot.reset_value = 1;
    assert(ls2k3000_periph_map(0x7a42, 0x80000, 1, &io) == -RT_EINVAL);
    assert(ls2k3000_periph_map(0x7a42, 0, 0x80001, &io) == -RT_EINVAL);
    assert(ls2k3000_periph_map(0x7a42, 0, 0, &io) == -RT_EINVAL);
    assert(ls2k3000_periph_map(0x7a42, 0, 8, NULL) == -RT_EINVAL);
    assert(ls2k3000_periph_map(0x1234, 0, 8, &io) == -RT_EINVAL);
    boot.pci_count = 1;
    boot.pci[0].id = 0x7a420014;
    bad_bar = 1;
    assert(ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &io) == -RT_EIO);
    bad_bar = 0;
    assert(ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &io) == -RT_EIO);
    pci_command = 2;
    assert(ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &io) == 0);
    assert((uintptr_t)io.context == 0x40050100);
    puts("PASS: arena bounds, PCI preference, fixed APB qualification and invalid resources");
    return 0;
}
'''


def main():
    bsp = Path(__file__).resolve().parents[2] / 'bsp/loongarch/ls2k3000'

    with tempfile.TemporaryDirectory(prefix='ls2k3000-resources-') as name:
        output = Path(name)
        (output / 'stub.h').write_text(STUB)
        (output / 'test.c').write_text(HARNESS)

        for header in ('rthw.h', 'rtthread.h', 'loongarch.h', 'ls2k3000_pci.h'):
            (output / header).write_text('')

        binary = output / 'test'
        subprocess.run([
            'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
            '-include', str(output / 'stub.h'), '-I' + str(output),
            '-I' + str(bsp / 'boot'), '-I' + str(bsp / 'drivers/boards'),
            str(output / 'test.c'), str(bsp / 'drivers/boards/periph_resource.c'),
            '-o', str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
