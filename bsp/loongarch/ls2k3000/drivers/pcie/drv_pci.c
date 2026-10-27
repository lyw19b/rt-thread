/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_pci.c
 * Author: LoongsonLab
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 */

/*
 * Description:
 *   Read retained PCI configuration using the LS2K3000 Type0/Type1 encoding.
 */

#include <rthw.h>
#include "board.h"
#include "ls2k3000_pci.h"

#ifdef BSP_USING_PCI_INVENTORY
static uint32_t read_config(const struct ls2k3000_pci_device *device, unsigned int offset)
{
    uint64_t address;

    if (!device || device->segment ||
        ls2k3000_pci_address(device->bus, device->devfn, offset, &address))
    {
        return UINT32_MAX;
    }

    return *(volatile uint32_t *)LOONGARCH_PHYS_UNCACHED(address);
}

const struct ls2k3000_pci_device *ls2k3000_pci_find(uint32_t class_code, unsigned int instance)
{
    for (uint32_t index = 0; index < ls2k3000_boot->pci_count; ++index)
    {
        const struct ls2k3000_pci_device *device = &ls2k3000_boot->pci[index];

        if ((device->class_revision >> 8) == class_code && !device->segment &&
            read_config(device, 0) == device->id)
        {
            if (!instance--)
            {
                return device;
            }
        }
    }

    return RT_NULL;
}

int ls2k3000_pci_map(const struct ls2k3000_pci_device *device, unsigned int bar,
                     uint64_t minimum, rt_ubase_t *base)
{
    uint32_t low;
    uint64_t address;

    if (!device || !base || device->header_type != 0 || bar > 5 ||
        device->bar[bar].bytes < minimum || !minimum ||
        read_config(device, 0) != device->id)
    {
        return -RT_EINVAL;
    }

    low = read_config(device, 0x10 + bar * 4);
    address = low & ~15U;

    if (low == UINT32_MAX || (low & 1) || (low & 6) == 2 || (low & 6) == 6)
    {
        return -RT_ENOSYS;
    }

    if ((low & 6) == 4)
    {
        if (bar == 5)
        {
            return -RT_EINVAL;
        }

        address |= (uint64_t)read_config(device, 0x14 + bar * 4) << 32;
    }

    if (!address || address != device->bar[bar].base ||
        address >= (UINT64_C(1) << 40) ||
        device->bar[bar].bytes > (UINT64_C(1) << 40) - address)
    {
        return -RT_EINVAL;
    }

    *base = LOONGARCH_PHYS_UNCACHED(address);
    return RT_EOK;
}

int ls2k3000_pci_command(const struct ls2k3000_pci_device *device,
                         uint16_t set, uint16_t clear)
{
    uint64_t address;
    uint32_t command;

    if (!device || device->segment || read_config(device, 0) != device->id ||
        ls2k3000_pci_address(device->bus, device->devfn, 4, &address))
    {
        return -RT_EINVAL;
    }

    command = ((read_config(device, 4) & 0xffff) | set) & ~clear;
    /* Upper status bits are W1C: write zero there, never echo their read value. */
    *(volatile uint32_t *)LOONGARCH_PHYS_UNCACHED(address) = command;
    rt_hw_dmb();
    return (read_config(device, 4) & 0xffff) == command ? RT_EOK : -RT_ERROR;
}

static long ls2k3000_pci_list(void)
{
    for (rt_uint32_t index = 0; index < ls2k3000_boot->pci_count; ++index)
    {
        const struct ls2k3000_pci_device *device = &ls2k3000_boot->pci[index];
        uint64_t address;
        rt_uint32_t id;

        if (device->segment ||
            ls2k3000_pci_address(device->bus, device->devfn, 0, &address))
        {
            rt_kprintf("PCI: unsupported segment %u\n", device->segment);
            continue;
        }

        id = *(volatile rt_uint32_t *)LOONGARCH_PHYS_UNCACHED(address);
        rt_kprintf("PCI %02x:%02x.%u firmware=%08x live=%08x class=%06x\n",
                   device->bus, device->devfn >> 3, device->devfn & 7,
                   device->id, id, device->class_revision >> 8);

        for (unsigned int bar = 0; bar < 6; ++bar)
        {
            if (device->bar[bar].bytes)
            {
                rt_kprintf("  BAR%u base=%lx size=%lx\n", bar,
                           (unsigned long)device->bar[bar].base,
                           (unsigned long)device->bar[bar].bytes);
            }
        }
    }

    return RT_EOK;
}

MSH_CMD_EXPORT(ls2k3000_pci_list, Compare firmware PCI inventory with native configuration reads);
#endif
