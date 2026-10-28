/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               implement Vanguard syscon reboot
 */

/*
 * Description:
 *   Use the Vanguard DTS syscon-reboot at WDT offset 0, mask 1. This is
 *   the software-reset bit, independent of the watchdog timer enable bit.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"

#if defined(BSP_USING_VANGUARD) && !defined(BSP_USING_LS2K0300_QEMU)
void rt_hw_cpu_reset(void)
{
    rt_ubase_t reset = LS2K_UNCACHED(0x16124000UL);

    rt_kprintf("REBOOT: Vanguard syscon software reset\n");
    rt_thread_mdelay(20);
    rt_hw_interrupt_disable();
    HWREG32(reset) |= 1U;
    rt_hw_dmb();

    for (;;)
    {
        __asm__ volatile("idle 0");
    }
}

#ifdef RT_USING_FINSH
MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_reset, reboot, Restart Vanguard through its reset controller);
#endif
#endif
