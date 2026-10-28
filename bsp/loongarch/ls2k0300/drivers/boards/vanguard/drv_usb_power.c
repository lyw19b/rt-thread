/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               supply Vanguard USB-A VBUS
 */

/*
 * Description:
 *   G2K03Z1 V1.1 schematic page 23 connects the USB-A power switch U12 EN
 *   to USB_DRIVEBUS (USB0/OTG), although the external hub uses USB1/EHCI.
 *   Supply VBUS through the idle DWC2 host port without starting OTG DMA.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"

#if defined(BSP_USING_USB_HOST) && defined(BSP_USING_VANGUARD) && \
    !defined(BSP_USING_LS2K0300_QEMU)
#include "ls2k_usb_platform.h"

#define VANGUARD_OTG_BASE          0x16040000UL
#define DWC2_GINTSTS               0x014U
#define DWC2_GSNPSID               0x040U
#define DWC2_HPRT                  0x440U
#define DWC2_HOST_MODE             (1U << 0)
#define DWC2_PORT_POWER            (1U << 12)
#define DWC2_PORT_WRITE_CLEAR      ((1U << 1) | (1U << 2) | (1U << 3) | (1U << 5))

rt_err_t ls2k_usb_board_power_on(void)
{
    rt_ubase_t base = LS2K_UNCACHED(VANGUARD_OTG_BASE);
    rt_uint32_t identity = HWREG32(base + DWC2_GSNPSID);
    rt_uint32_t port;

    if ((identity & 0xfffff000U) != 0x4f542000U &&
        (identity & 0xfffff000U) != 0x4f543000U)
    {
        rt_kprintf("Vanguard USB: unexpected DWC2 identity %08x\n", identity);
        return -RT_ENOSYS;
    }

    /* Firmware supplies the PHY reference clock and host role. Do not
     * force a role change or reset the shared PHY while EHCI owns USB1. */
    if (!(HWREG32(base + DWC2_GINTSTS) & DWC2_HOST_MODE))
    {
        rt_kprintf("Vanguard USB: firmware must select OTG host mode for VBUS\n");
        return -RT_ERROR;
    }

    port = HWREG32(base + DWC2_HPRT);

    /* HPRT includes W1C change flags and a write-one-disable enable bit.
     * Exclude them when changing power, as in the DWC2 host adapter. */
    HWREG32(base + DWC2_HPRT) = (port & ~DWC2_PORT_WRITE_CLEAR) | DWC2_PORT_POWER;
    rt_hw_dmb();

    if (!(HWREG32(base + DWC2_HPRT) & DWC2_PORT_POWER))
    {
        return -RT_EIO;
    }

    rt_thread_mdelay(100);
    rt_kprintf("Vanguard USB: USB-A VBUS on through OTG, port %08x -> %08x\n",
               port, HWREG32(base + DWC2_HPRT));

    return RT_EOK;
}
#endif
