/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     LoongsonLab               share EHCI through board resources
 *
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/* Description: Check LS2K0300 firmware clocks without resetting the shared PHY. */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"
#include "ls2k_usb_platform.h"

#ifdef BSP_USING_USB_HOST
rt_err_t ls2k_usb_platform_init(rt_ubase_t *base, int *irq, unsigned int *hardware_ports)
{
    if ((HWREG32(LOONGARCH_PHYS_UNCACHED(0x16000114UL)) & 0x180) != 0x180 ||
        (HWREG32(LOONGARCH_PHYS_UNCACHED(0x1600011cUL)) & 0x180) != 0x180)
    {
        rt_kprintf("USB: firmware must enable USB/shared clocks and release reset\n");
        return -RT_ERROR;
    }

    *base = LOONGARCH_PHYS_UNCACHED(0x16080000UL);
    *irq = 46;
    *hardware_ports = 1;
    return RT_EOK;
}
#endif
