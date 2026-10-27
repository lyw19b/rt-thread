/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: boot_contract.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Validate firmware resources without dereferencing any device addresses.
 */

#include "boot_info.h"

uint64_t ls2k3000_boot_checksum(const struct ls2k3000_boot_info *info)
{
    const uint8_t *data = (const void *)info;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t index = 0; index < offsetof(struct ls2k3000_boot_info, checksum); ++index)
    {
        hash = (hash ^ data[index]) * UINT64_C(1099511628211);
    }

    return hash;
}

int ls2k3000_framebuffer_validate(const struct ls2k3000_framebuffer *fb)
{
    uint64_t required;

    if (!fb || !fb->base || (fb->base & 3) || !fb->width || !fb->height ||
        fb->width > 8192 || fb->height > 8192 || fb->stride < fb->width ||
        fb->stride > 16384 || fb->format > 1)
    {
        return -1;
    }

    required = (uint64_t)fb->stride * fb->height * 4;

    if (required > fb->bytes || fb->base >= (UINT64_C(1) << 40) ||
        fb->bytes > (UINT64_C(1) << 40) - fb->base)
    {
        return -1;
    }

    if (fb->base < LS2K3000_KERNEL_PHYS + LS2K3000_RESERVED_BYTES &&
        fb->base + fb->bytes > LS2K3000_KERNEL_PHYS)
    {
        return -1;
    }

    return 0;
}

int ls2k3000_boot_validate(const struct ls2k3000_boot_info *info)
{
    int covered = 0;

    if (!info || info->magic != LS2K3000_BOOT_MAGIC ||
        info->version != LS2K3000_BOOT_VERSION || info->bytes != sizeof(*info) ||
        info->kernel_base != LS2K3000_KERNEL_PHYS ||
        info->reserved_bytes != LS2K3000_RESERVED_BYTES ||
        !info->kernel_bytes || info->kernel_bytes > LS2K3000_IMAGE_MAX ||
        !info->memory_count || info->memory_count > LS2K3000_MEMORY_MAX ||
        info->pci_count > LS2K3000_PCI_MAX)
    {
        return -1;
    }

    if (info->checksum != ls2k3000_boot_checksum(info))
    {
        return -1;
    }

    /* The kernel may use only the region the loader explicitly reserved. */
    for (uint32_t index = 0; index < info->memory_count; ++index)
    {
        const struct ls2k3000_memory *region = &info->memory[index];

        if ((region->base & 4095) || !region->pages ||
            region->pages > (UINT64_MAX - region->base) / 4096)
        {
            return -1;
        }

        if (region->base < LS2K3000_KERNEL_PHYS + LS2K3000_RESERVED_BYTES &&
            region->base + region->pages * 4096 > LS2K3000_KERNEL_PHYS &&
            (region->type != 2 || (region->attributes & (UINT64_C(1) << 63))))
        {
            return -1;
        }

        if (region->type == 2 && region->base <= LS2K3000_KERNEL_PHYS &&
            region->base + region->pages * 4096 >=
                LS2K3000_KERNEL_PHYS + LS2K3000_RESERVED_BYTES &&
            !(region->attributes & (UINT64_C(1) << 63)))
        {
            covered = 1;
        }
    }

    return covered ? 0 : -1;
}

int ls2k3000_pci_address(unsigned int bus, unsigned int devfn,
                        unsigned int offset, uint64_t *address)
{
    if (!address || bus > 255 || devfn > 255 || offset > 4092 || (offset & 3))
    {
        return -1;
    }

    /* User manual 5.3.1: offset[11:8] occupies physical address[27:24]. */
    *address = UINT64_C(0xfe00000000) | (bus ? UINT64_C(0x10000000) : 0) |
               ((uint64_t)bus << 16) | ((uint64_t)devfn << 8) |
               ((uint64_t)(offset & 0xf00) << 16) | (offset & 0xff);
    return 0;
}
