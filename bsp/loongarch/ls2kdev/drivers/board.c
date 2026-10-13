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
#include "drv_interrupt.h"
#include "drv_timer.h"
#include "drv_power.h"
#include "timer.h"

void rt_hw_board_init(void)
{
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
