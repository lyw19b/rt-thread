/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */


#ifndef BOARD_H__
#define BOARD_H__

#include <rtconfig.h>

#include "irq.h"

extern unsigned int __bss_start;
extern unsigned int __bss_end;

#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0x0
#endif

#define RT_HW_MPR_SIZE  (64 * 1024 * 1024)

#define RT_HW_HEAP_BEGIN ((void *)&__bss_end)
#define RT_HW_HEAP_END   ((void *)(RT_HW_HEAP_BEGIN + 64 * 1024 * 1024))
#define RT_HW_PAGE_START RT_HW_HEAP_END
#define RT_HW_PAGE_END   ((void *)(0x9000000000000000UL + (192 * 1024 * 1024)))

void rt_hw_board_init(void);

#endif
