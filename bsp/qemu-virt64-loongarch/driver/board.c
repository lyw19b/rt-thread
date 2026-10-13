/*
 * Copyright (c) 2006-2020, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-01-30     lizhirui     first version
 */

#include <rthw.h>
#include <rtthread.h>
#ifndef RT_USING_NANO
#include <rtdevice.h>
#endif

#include "board.h"
#include "timer.h"
#ifndef RT_USING_NANO
#include "drv_uart.h"
#endif

#ifdef RT_USING_SMART
#include <mm_aspace.h>
#include <mm_page.h>

#include "mmu.h"
#include "lwp_arch.h"
#endif


#define IOREMAP_SIZE (16UL * 1024 * 1024)
#define IOREMAP_VSTART 0xffffffc010000000UL
#define KERNEL_PVADDR_OFFSET (-0x9000000000000000UL)

#ifdef RT_USING_SMART
extern rt_ubase_t MMUTable[];
#endif

void rt_hw_board_init(void)
{
    // We must set pv offset before init.
#ifdef RT_USING_SMART
    rt_kmem_pvoff_set(KERNEL_PVADDR_OFFSET);
#endif

#ifdef RT_USING_SMART
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)IOREMAP_VSTART,
                       IOREMAP_SIZE, MMUTable, PV_OFFSET);
    rt_region_t pages = {(rt_size_t)RT_HW_PAGE_START, (rt_size_t)RT_HW_PAGE_END};
    rt_page_init(pages);
    rt_hw_mmu_setup(&rt_kernel_space, RT_NULL, 0);
#endif

#ifdef RT_USING_HEAP
    /* initialize memory system */
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);
#endif

    rt_hw_interrupt_init();

#ifndef RT_USING_NANO
    rt_hw_uart_init();
#endif

#if defined(RT_USING_CONSOLE) && !defined(RT_USING_NANO)
    /* set console device */
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif /* RT_USING_CONSOLE */

    rt_hw_timer_init();



#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#ifdef RT_USING_HEAP
    rt_kprintf("heap: [0x%08x - 0x%08x]\n", (rt_ubase_t)RT_HW_HEAP_BEGIN, (rt_ubase_t)RT_HW_HEAP_END);
#endif /* RT_USING_HEAP */
}

void rt_hw_cpu_reset(void)
{
    while (1);
}

MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_reset, reboot, reset machine);
