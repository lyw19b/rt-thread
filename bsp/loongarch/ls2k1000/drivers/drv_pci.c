/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include "ls2k1000la.h"
#include "drv_pci.h"

static rt_err_t _memory_bar(rt_ubase_t config, unsigned int index, rt_ubase_t *base)
{
    rt_uint32_t bar;
    rt_uint64_t physical;

    if (!base || index >= 6 || *(volatile rt_uint16_t *)config == 0xffff)
        return -RT_EINVAL;
    bar = *(volatile rt_uint32_t *)(config + 0x10 + index * 4);
    if (!bar || bar == 0xffffffff || (bar & 1))
        return -RT_ENOSYS;
    physical = bar & ~0xfU;
    if ((bar & 6) == 4)
    {
        if (index == 5)
            return -RT_EINVAL;
        physical |= (rt_uint64_t)*(volatile rt_uint32_t *)(config + 0x14 + index * 4) << 32;
    }
    else if (bar & 6)
        return -RT_ENOSYS;
    if (!physical || (physical & ~TO_PHYS_MASK))
        return -RT_EINVAL;
    *base = PHYS_TO_UNCACHED(physical);
    return RT_EOK;
}

rt_uint64_t pci_get_device_map_addr(rt_uint64_t bus, rt_uint64_t device,
                                   rt_uint64_t function, rt_uint64_t index)
{
    rt_ubase_t config, base;

    if (bus > 255 || device > 31 || function > 7 || index > 5)
        return 0;
    config = PHYS_TO_UNCACHED(0xfe00000000UL | (bus << 16) | (device << 11) | (function << 8));
    return _memory_bar(config, index, &base) == RT_EOK ? base : 0;
}

rt_err_t ls2k_pci_enable_gmac(unsigned int port, rt_ubase_t *base)
{
    rt_ubase_t config;
    rt_uint16_t command;
    rt_err_t error;

    if (port > 1)
        return -RT_EINVAL;
    config = port ? LS_PCIE_GMAC1_ADDR : LS_PCIE_GMAC0_ADDR;
    error = _memory_bar(config, 0, base);
    if (error)
        return error;
    /* Preserve firmware resource assignment. Use a 16-bit access so PCI
     * status (write-one-to-clear, adjacent to command) is never written. */
    command = *(volatile rt_uint16_t *)(config + 4);
    command = (command | 6U) & ~(1U << 10); /* memory, bus master, legacy IRQ */
    *(volatile rt_uint16_t *)(config + 4) = command;
    __asm__ volatile("dbar 0" ::: "memory");
    if ((*(volatile rt_uint16_t *)(config + 4) & 6) != 6)
        return -RT_ERROR;
    return RT_EOK;
}
