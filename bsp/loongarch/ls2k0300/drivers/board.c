/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rthw.h>
#include <rtthread.h>
#include "board.h"
#include "platform_interrupt.h"
#include "ls2k_uart.h"
#include "timer.h"
#ifdef RT_USING_SMART
#include <mm_aspace.h>
#include <mm_page.h>
#include "mmu.h"
#include "lwp_arch.h"
extern rt_ubase_t MMUTable[];
#endif

void rt_hw_board_init(void)
{
#ifdef RT_USING_SMART
    rt_region_t pages =
    {
        .start = (rt_size_t)RT_HW_PAGE_START,
        .end = (rt_size_t)RT_HW_PAGE_END,
        .name = "low DDR",
    };

    rt_kmem_pvoff_set(-0x9000000000000000UL);
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)0xffffffc010000000UL,
                       16UL * 1024 * 1024, MMUTable, PV_OFFSET);
    rt_page_init(pages);
    rt_hw_mmu_setup(&rt_kernel_space, RT_NULL, 0);
#endif
#ifdef RT_USING_HEAP
    RT_ASSERT((rt_ubase_t)RT_HW_HEAP_BEGIN < (rt_ubase_t)RT_HW_HEAP_END);
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);
#endif
    rt_hw_interrupt_init();
    rt_hw_uart_init();
#if defined(RT_USING_CONSOLE) && !defined(RT_USING_NANO)
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif
    rt_hw_timer_init();
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif
}

void rt_hw_cpu_reset(void)
{
    /* Reset/PMIC wiring is board specific; no guessed reset register writes. */
    rt_kprintf("Reset through the board reset button or firmware.\n");
}
