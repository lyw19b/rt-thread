/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Expose the validated firmware handoff and reserved memory.
 */

#ifndef LS2K3000_BOARD_H
#define LS2K3000_BOARD_H

#include <rtthread.h>
#include "kernel_map.h"
#include "boot_info.h"

extern unsigned char _end[];
extern const struct ls2k3000_boot_info *ls2k3000_boot;

#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0
#endif

#define RT_HW_HEAP_BEGIN ((void *)_end)
#define RT_HW_HEAP_END ((void *)(LOONGARCH_CACHED_BASE + LS2K3000_DMA_PHYS))
#define RT_HW_PAGE_START ((void *)(LOONGARCH_CACHED_BASE + (LS2K3000_DMA_PHYS + LS2K3000_DMA_BYTES)))
#define RT_HW_PAGE_END ((void *)(LOONGARCH_CACHED_BASE + LS2K3000_HANDOFF_PHYS))

void ls2k3000_uart_init(void);
void ls2k3000_early_puts(const char *text);
void ls2k3000_boot_status(void);
long ls2k3000_display_test(void);
int ls2k3000_dma_init(void);
void *ls2k3000_dma_alloc(rt_size_t bytes, rt_size_t alignment, uint64_t *physical);

#endif
