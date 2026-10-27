/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_power.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Reset the Explorer through the ACPI-validated native reset register.
 */

#include <rthw.h>
#include "board.h"

static long ls2k3000_reboot(void)
{
    rt_base_t level;
    volatile rt_uint32_t *reset;

    if (ls2k3000_boot->reset_address != UINT64_C(0x10150030) ||
        ls2k3000_boot->reset_bits != 32 || ls2k3000_boot->reset_value != 1)
    {
        rt_kprintf("REBOOT: firmware reset register was not validated\n");
        return -RT_ENOSYS;
    }

    rt_kprintf("REBOOT: LS2K3000 RST_CNT.OS_RST at 0x10150030\n");
    rt_thread_mdelay(50);
    reset = (void *)LOONGARCH_PHYS_UNCACHED(ls2k3000_boot->reset_address);
    level = rt_hw_interrupt_disable();
    rt_hw_dmb();
    *reset = ls2k3000_boot->reset_value;
    rt_hw_dmb();

    for (volatile unsigned int attempt = 0; attempt < 1000000; ++attempt)
    {
        __asm__ volatile("nop");
    }

    rt_hw_interrupt_enable(level);
    rt_kprintf("REBOOT: reset request returned without restarting the board\n");
    return -RT_ERROR;
}

void rt_hw_cpu_reset(void)
{
    (void)ls2k3000_reboot();
}

MSH_CMD_EXPORT_ALIAS(ls2k3000_reboot, reboot, Restart board through native reset register);
