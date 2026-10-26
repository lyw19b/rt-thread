/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include "board.h"
#include "loongarch.h"
#include "drv_uart.h"
#include "platform_interrupt.h"
#include "drv_timer.h"
#include "drv_power.h"
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
        .name = "high DDR",
    };

    rt_kmem_pvoff_set(-LOONGARCH_CACHED_BASE);
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)0xffffffc010000000UL,
                       16UL * 1024 * 1024, MMUTable, PV_OFFSET);
    rt_page_init(pages);
    rt_hw_mmu_setup(&rt_kernel_space, RT_NULL, 0);
#endif
    RT_ASSERT((rt_ubase_t)RT_HW_HEAP_BEGIN < (rt_ubase_t)RT_HW_HEAP_END);
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);
    rt_hw_interrupt_init();
    rt_hw_uart_init();
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
    rt_hw_timer_init();
    rt_thread_idle_sethook(rt_hw_cpu_idle);
    rt_components_board_init();

    rt_kprintf("LS2K1000LA: PRID=%08x timer=%lu Hz\n",
               read_cpucfg(LOONGARCH_CPUCFG0), read_const_freq());
    rt_kprintf("heap: [%p - %p]\n", RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);
}
