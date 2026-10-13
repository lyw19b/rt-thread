/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#include <rtthread.h>

#include "rtconfig.h"
#include "timer.h"
#include "loongarch.h"

/**
 * This function will initial OS timer
 */
rt_weak void rt_hw_timer_init(void)
{
    rt_uint64_t timer_config;
    rt_uint64_t int_val;

    int_val = CPU_HZ / 2 / RT_TICK_PER_SECOND;

    timer_config = int_val & CSR_TCFG_VAL;

    // turn off periodic
    timer_config &= ~CSR_TCFG_PERIOD;

    // turn on timer
    timer_config |= CSR_TCFG_EN;

    csr_write64(timer_config, LOONGARCH_CSR_TCFG);
    set_csr_ecfg(ECFGF_TIMER);
}


/**
 * This is the timer interrupt service routine.
 */
rt_weak void rt_hw_timer_handler(void)
{
    unsigned long timer_config;

    timer_config = (CPU_HZ / 2 / RT_TICK_PER_SECOND) & CSR_TCFG_VAL;

    timer_config |= CSR_TCFG_EN;

    timer_config &= ~CSR_TCFG_PERIOD;

    csr_write64(timer_config, LOONGARCH_CSR_TCFG);

    /* increase a OS tick */
    rt_tick_increase();
}
