/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     LoongsonLab               share EHCI through board resources
 *
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 * 2026-10-28     LoongsonLab               apply board-specific USB power wiring
 */

/*
 * Description:
 *   Keep the host PHY awake when EHCI takes ownership from firmware. Disable
 *   OHCI DMA schedules, preserve PHY tuning and reference clocks, and apply
 *   the selected board's external USB power wiring.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"
#include "ls2k_usb_platform.h"

#ifdef BSP_USING_USB_HOST
rt_weak rt_err_t ls2k_usb_board_prepare(void)
{
    return RT_EOK;
}

rt_weak rt_err_t ls2k_usb_board_power_on(void)
{
    return RT_EOK;
}

rt_err_t ls2k_usb_platform_init(rt_ubase_t *base, int *irq, unsigned int *hardware_ports)
{
    rt_ubase_t companion = LOONGARCH_PHYS_UNCACHED(0x16088000UL);
    rt_ubase_t phy = LOONGARCH_PHYS_UNCACHED(0x16000508UL);
    const rt_uint32_t host_override = (1U << 28) | (1U << 30);
    rt_uint32_t control;
    rt_uint32_t phy_control;
    rt_uint32_t revision;
    rt_err_t error;

    error = ls2k_usb_board_prepare();
    if (error != RT_EOK)
    {
        return error;
    }

    if ((HWREG32(LOONGARCH_PHYS_UNCACHED(0x16000114UL)) & 0x180) != 0x180 ||
        (HWREG32(LOONGARCH_PHYS_UNCACHED(0x1600011cUL)) & 0x180) != 0x180)
    {
        rt_kprintf("USB: firmware must enable USB/shared clocks and release reset\n");
        return -RT_ERROR;
    }

#ifdef BSP_USING_LS2K0302
    rt_kprintf("2K0302 USB: reading OHCI revision at %p (32-bit)\n", (void *)companion);
#endif
    revision = HWREG32(companion);
#ifdef BSP_USING_LS2K0302
    rt_kprintf("2K0302 USB: OHCI revision %08x\n", revision);
#endif
    if ((revision & 0xff) != 0x10)
    {
        rt_kprintf("USB: unexpected OHCI companion revision\n");
        return -RT_ENOSYS;
    }

    /* Quiesce the companion without running any of its DMA lists. Clearing
     * HCFS alone does not restore EHCI connection detection on LS2K0300. */
    rt_hw_interrupt_mask(47);
    HWREG32(companion + 0x14) = 0xffffffffU;
    control = HWREG32(companion + 4);
    HWREG32(companion + 4) = control & ~0xfcU;
    rt_hw_dmb();

    if (HWREG32(companion + 4) & 0xfcU)
    {
        return -RT_ERROR;
    }

    rt_kprintf("USB companion: control %08x -> %08x, schedules disabled\n",
               control, HWREG32(companion + 4));

    /* SPL uses host level bit 30=1, but delegates suspend to the controllers
     * with bit 28=0. After usb stop that leaves CCS=0 on this board. Select
     * the same awake level under software control before starting EHCI.
     * No PHY reset or clock/pulldown/OTG settings are changed. */
    phy_control = HWREG32(phy);
    HWREG32(phy) = phy_control | host_override;
    rt_hw_dmb();

    if ((HWREG32(phy) & host_override) != host_override)
    {
        return -RT_ERROR;
    }

    rt_thread_mdelay(20);
    rt_kprintf("USB host PHY: %08x -> %08x, software awake\n",
               phy_control, HWREG32(phy));

    error = ls2k_usb_board_power_on();

    if (error != RT_EOK)
    {
        return error;
    }

    *base = LOONGARCH_PHYS_UNCACHED(0x16080000UL);
    *irq = 46;
    *hardware_ports = 1;
    return RT_EOK;
}
#endif
