/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Fixed-width, versioned handoff shared by the EFI loader and the kernel.
 */

#ifndef LS2K3000_BOOT_INFO_H
#define LS2K3000_BOOT_INFO_H

#include <stdint.h>
#include <stddef.h>

#define LS2K3000_BOOT_MAGIC       UINT64_C(0x4c53334b55454649)
#define LS2K3000_BOOT_VERSION     3U
#define LS2K3000_KERNEL_PHYS      UINT64_C(0x02000000)
#define LS2K3000_RESERVED_BYTES   UINT64_C(0x0e000000)
#define LS2K3000_IMAGE_MAX        UINT64_C(0x04000000)
#define LS2K3000_DMA_PHYS         UINT64_C(0x09e00000)
#define LS2K3000_DMA_BYTES        UINT64_C(0x00200000)
#define LS2K3000_HANDOFF_PHYS     UINT64_C(0x0fff0000)
#define LS2K3000_HANDOFF_BYTES    UINT64_C(0x00010000)
#define LS2K3000_MEMORY_MAX       256U
#define LS2K3000_PCI_MAX          64U

struct ls2k3000_memory
{
    uint64_t base;
    uint64_t pages;
    uint64_t attributes;
    uint32_t type;
    uint32_t reserved;
};

struct ls2k3000_pci_resource
{
    uint64_t base;
    uint64_t bytes;
};

struct ls2k3000_pci_device
{
    uint16_t segment;
    uint8_t bus;
    uint8_t devfn;
    uint32_t id;
    uint32_t class_revision;
    uint32_t header_type;
    struct ls2k3000_pci_resource bar[6];
};

struct ls2k3000_framebuffer
{
    uint64_t base;
    uint64_t bytes;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

struct ls2k3000_boot_info
{
    uint64_t magic;
    uint32_t version;
    uint32_t bytes;
    uint64_t kernel_base;
    uint64_t reserved_bytes;
    uint64_t kernel_bytes;
    struct ls2k3000_framebuffer framebuffer;
    uint64_t gop_status;
    uint32_t gop_handles;
    uint32_t gop_masks[4];
    uint32_t reserved;
    struct ls2k3000_framebuffer gop_raw;
    uint64_t reset_address;
    uint32_t reset_value;
    uint32_t reset_bits;
    uint32_t memory_count;
    uint32_t pci_count;
    struct ls2k3000_memory memory[LS2K3000_MEMORY_MAX];
    struct ls2k3000_pci_device pci[LS2K3000_PCI_MAX];
    uint64_t checksum;
};

_Static_assert(sizeof(struct ls2k3000_boot_info) <= LS2K3000_HANDOFF_BYTES,
               "Handoff exceeds reserved storage");

uint64_t ls2k3000_boot_checksum(const struct ls2k3000_boot_info *info);
int ls2k3000_boot_validate(const struct ls2k3000_boot_info *info);
int ls2k3000_framebuffer_validate(const struct ls2k3000_framebuffer *fb);
int ls2k3000_pci_address(unsigned int bus, unsigned int devfn,
                        unsigned int offset, uint64_t *address);

#endif
