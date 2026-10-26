/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    add Xingyun Smart board setup
 *
 * Description:
 *   Define board memory limits without consuming firmware framebuffer RAM.
 */

#ifndef __LS2K_BOARD_H__
#define __LS2K_BOARD_H__

#include <rtconfig.h>
#include "kernel_map.h"

extern unsigned int __bss_start;
extern unsigned int __bss_end;
extern unsigned char _end[];

#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0x0
#endif

#define RT_HW_HEAP_SIZE  64 * 1024 * 1024
#define RT_HW_HEAP_BEGIN ((void *)_end)
#ifdef RT_USING_SMART
/* High DDR only; low DDR remains owned by firmware and its framebuffer. */
#define RT_HW_HEAP_END   ((void *)(LOONGARCH_CACHED_BASE + 0xa0000000UL))
#define RT_HW_PAGE_START RT_HW_HEAP_END
#define RT_HW_PAGE_END   ((void *)(LOONGARCH_CACHED_BASE + 0xb8000000UL))
#else
#define RT_HW_HEAP_END   ((void *)((char *)RT_HW_HEAP_BEGIN + RT_HW_HEAP_SIZE))
#endif

void rt_hw_board_init(void);

#endif // __LS2K_BOARD_H__
