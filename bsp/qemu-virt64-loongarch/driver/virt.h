/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-02-17     GuEe-GUI     the first version
 */

#ifndef VIRT_H__
#define VIRT_H__

#include <rtdef.h>

#ifdef RT_USING_SMART
#include <mmu.h>
#include <ioremap.h>

#endif

/* VirtIO */
#define VIRTIO_MMIO_BASE    0x8000000030001000
#define VIRTIO_MMIO_SIZE    0x00001000
#define VIRTIO_MAX_NR       6
#define VIRTIO_IRQ_BASE     0x10
#define VIRTIO_VENDOR_ID    0x554d4551  /* "QEMU" */

#define MAX_HANDLERS        128
#endif
