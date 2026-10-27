/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: periph_resource.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Map retained PCI resources and use the CPU constant counter for timeouts.
 */

#include <rthw.h>
#include "board.h"
#include "loongarch.h"
#include "ls2k3000_pci.h"
#include "periph_resource.h"

#ifdef BSP_USING_LS2K3000_PERIPHERALS
static uint32_t native_read(void *context, unsigned int offset, unsigned int width)
{
    rt_ubase_t address = (rt_ubase_t)context + offset;
    uint32_t value;

    rt_hw_dmb();
    value = width == 1 ? *(volatile uint8_t *)address : *(volatile uint32_t *)address;
    rt_hw_dmb();
    return value;
}

static void native_write(void *context, unsigned int offset, unsigned int width,
                          uint32_t value)
{
    rt_ubase_t address = (rt_ubase_t)context + offset;

    rt_hw_dmb();

    if (width == 1)
    {
        *(volatile uint8_t *)address = value;
    }
    else
    {
        *(volatile uint32_t *)address = value;
    }

    rt_hw_dmb();
}

static uint64_t native_now(void *context)
{
    uint64_t ticks = drdtime();
    uint64_t frequency = read_cpucfg(LOONGARCH_CPUCFG4);
    uint32_t ratio = read_cpucfg(LOONGARCH_CPUCFG5);

    (void)context;
    frequency = frequency * (ratio & 0xffff) / (ratio >> 16);
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

void ls2k3000_periph_fixed(uint64_t address, struct ls_periph_io *io)
{
    io->context = (void *)LOONGARCH_PHYS_UNCACHED(address);
    io->read = native_read;
    io->write = native_write;
    io->now_us = native_now;
}

int ls2k3000_periph_map(uint16_t pci_id, unsigned int offset,
                       unsigned int bytes, struct ls_periph_io *io)
{
    unsigned int aperture = pci_id == 0x7a42 ? 0x80000 : 0x1000;

    if (!io || (pci_id != 0x7a42 && pci_id != 0x7a1b) || !bytes ||
        bytes > aperture || offset > aperture - bytes)
    {
        return -RT_EINVAL;
    }

    for (uint32_t i = 0; i < ls2k3000_boot->pci_count; ++i)
    {
        const struct ls2k3000_pci_device *device = &ls2k3000_boot->pci[i];
        rt_ubase_t base;

        if (device->id != ((uint32_t)pci_id << 16 | 0x0014))
        {
            continue;
        }

        if (ls2k3000_pci_map(device, 0, offset + bytes, &base))
        {
            return -RT_EIO;
        }

        /* The validated handoff must already have memory decoding enabled. */
        uint64_t config;

        if (ls2k3000_pci_address(device->bus, device->devfn, 4, &config) ||
            !(*(volatile uint32_t *)LOONGARCH_PHYS_UNCACHED(config) & 2))
        {
            return -RT_EIO;
        }

        io->context = (void *)(base + offset);
        io->read = native_read;
        io->write = native_write;
        io->now_us = native_now;
        return RT_EOK;
    }

    return -RT_ENOSYS;
}

rt_err_t ls2k3000_periph_error(int result)
{
    switch (result)
    {
    case LS_OK:
        return RT_EOK;
    case LS_INVALID:
        return -RT_EINVAL;
    case LS_TIMEOUT:
        return -RT_ETIMEOUT;
    case LS_BUSY:
        return -RT_EBUSY;
    case LS_UNSUPPORTED:
        return -RT_ENOSYS;
    default:
        return -RT_EIO;
    }
}
#endif
