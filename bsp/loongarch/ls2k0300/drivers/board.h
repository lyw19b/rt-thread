/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K0300_BOARD_H__
#define LS2K0300_BOARD_H__
#include <rtconfig.h>

extern unsigned char _end[];
#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0
#endif

#define RT_HW_HEAP_BEGIN ((void *)_end)
#define RT_HW_HEAP_END   ((void *)0x9000000002000000UL)
#define RT_HW_PAGE_START RT_HW_HEAP_END
#define RT_HW_PAGE_END   ((void *)(0x9000000000000000UL + BSP_RAM_END))

void rt_hw_board_init(void);
#endif
