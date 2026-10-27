/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               standardize file header
 * 2026-10-27     LoongsonLab               implement constant-counter microsecond delay
 */

/*
 * Description:
 *   Program the local tick timer and provide delays from the constant counter.
 */

#include <rtthread.h>
#include "cpuport.h"
#include "timer.h"
#include "loongarch.h"

void rt_hw_us_delay(rt_uint32_t us)
{
    rt_uint64_t base;
    rt_uint32_t ratio;
    rt_uint32_t mult;
    rt_uint32_t div;
    rt_uint64_t frequency;
    rt_uint64_t ticks;
    rt_uint64_t start;

    if (!us)
    {
        return;
    }

    base = read_cpucfg(LOONGARCH_CPUCFG4);
    ratio = read_cpucfg(LOONGARCH_CPUCFG5);

    mult = ratio & 0xffff;
    div = ratio >> 16;

    RT_ASSERT(base && mult && div);

    frequency = base * mult / div;

    /* Round up without overflowing frequency * us. The constant counter
     * continues running with interrupts disabled and before scheduler start. */
    ticks = (frequency / 1000000) * us;
    ticks += ((frequency % 1000000) * us + 999999) / 1000000;
    start = drdtime();

    while ((rt_uint64_t)(drdtime() - start) < ticks)
    {
        __asm__ volatile("" ::: "memory");
    }
}

/**
 * @brief    Program the local periodic timer for RT_TICK_PER_SECOND ticks.
 *
 * @note     Reads the constant-counter frequency from CPUCFG4/5. Each CPU calls
 *           this during startup; the CPU interrupt dispatcher acknowledges each tick.
 */
rt_weak void rt_hw_timer_init(void)
{
    rt_uint64_t base = read_cpucfg(LOONGARCH_CPUCFG4);
    rt_uint32_t ratio = read_cpucfg(LOONGARCH_CPUCFG5);
    rt_uint32_t mult = ratio & 0xffff;
    rt_uint32_t div = ratio >> 16;
    rt_uint64_t interval;

    RT_ASSERT(base && mult && div);
    interval = base * mult / div / RT_TICK_PER_SECOND;
    
    /* TCFG stores the interval in bits 63:2. Periodic mode avoids ISR drift. */
    csr_write64((interval & CSR_TCFG_VAL) | CSR_TCFG_PERIOD | CSR_TCFG_EN,
                LOONGARCH_CSR_TCFG);
    
    set_csr_ecfg(ECFGF_TIMER);
}

/**
 * @brief    Advance the RT-Thread tick from the local timer interrupt.
 *
 * @note     The caller has acknowledged the timer and entered interrupt context.
 *           In SMP builds, rt_tick_increase() handles per-CPU and global tick accounting.
 */
rt_weak void rt_hw_timer_handler(void)
{
    rt_tick_increase();
}
