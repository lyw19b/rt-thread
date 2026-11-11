/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-10     OpenLANPU Contributors    own physical 2K0302 USB clock handoff
 */

/*
 * Description:
 *   X2K032Z0 USB Host clocks/reset and the SoC software-reset command.
 * Notes:
 *   LS2K0300 user manual V1.01 tables 3-11/3-13/3-35/3-43 and 26-3.
 *   Board schematic sheet 21: USB_PWR switch U29 EN# is grounded. No OTG
 *   power command or GPIO mux is needed. Use the vendor SPL PHY sequence
 *   and preserve all unrelated clocks and configuration fields.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"

#ifdef BSP_USING_LS2K0302

#define CHIP_CLOCK_CONTROL  LS2K_UNCACHED(0x16000114UL)
#define CHIP_RESET_CONTROL  LS2K_UNCACHED(0x1600011cUL)
#define PLL_CLOCK_ENABLE    LS2K_UNCACHED(0x16000424UL)
#define USB_PHY_CONTROL     LS2K_UNCACHED(0x16000508UL)
#define USB_PHY_DATA        LS2K_UNCACHED(0x16000500UL)
#define USB_PHY_COMMAND     LS2K_UNCACHED(0x16000504UL)
#define SOC_SOFTWARE_RESET  LS2K_UNCACHED(0x16124000UL)
#define USB_MODULE_MASK     ((1U << 7) | (1U << 8))
#define USB_SOURCE_ENABLE   (1U << 2)
#define USB_PHY_20MHZ       (1U << 3)
#define USB_PHY_RESET       (1U << 27)
#define USB_UTMI_RELEASE    (1U << 1)
#define USB_HOST_SUSPEND    (1U << 30)
#define USB_HOST_OVERRIDE   (1U << 28)

#ifdef BSP_USING_USB_HOST
#include "ls2k_usb_platform.h"

static rt_err_t usb_phy_transaction(rt_uint32_t command)
{
    /* Vendor SPL uses PHY address 0x18 through this configuration bridge.
     * Bound its completion wait so a missing PHY does not stall UI startup. */
    HWREG32(USB_PHY_COMMAND) = command | (1U << 27);
    rt_hw_dmb();
    HWREG32(USB_PHY_COMMAND) = command;
    rt_hw_dmb();
    for (unsigned int attempt = 0; attempt < 100; ++attempt)
    {
        if (HWREG32(USB_PHY_COMMAND) & (1U << 28))
        {
            return RT_EOK;
        }
        rt_thread_mdelay(1);
    }
    rt_kprintf("2K0302 USB: PHY configuration timed out (command %08x)\n", command);
    return -RT_EIO;
}

rt_err_t ls2k_usb_board_prepare(void)
{
    rt_uint32_t clocks = HWREG32(CHIP_CLOCK_CONTROL);
    rt_uint32_t resets = HWREG32(CHIP_RESET_CONTROL);
    rt_uint32_t source = HWREG32(PLL_CLOCK_ENABLE);
    rt_uint32_t phy;
    rt_err_t error;

    /* Enable only the USB source, common fabric and EHCI/OHCI module clocks.
     * Keep the host controller reset until its 24 MHz PHY is running. */
    HWREG32(PLL_CLOCK_ENABLE) = source | USB_SOURCE_ENABLE;
    HWREG32(CHIP_CLOCK_CONTROL) = clocks | USB_MODULE_MASK;
    rt_hw_dmb();
    rt_thread_mdelay(1);
    /* Releasing the shared reset restores PHY register defaults. Configure
     * the reference and UTMI only after that release, otherwise they are lost. */
    HWREG32(CHIP_RESET_CONTROL) = (resets | (1U << 7)) & ~(1U << 8);
    rt_hw_dmb();
    rt_thread_mdelay(1);
    phy = HWREG32(USB_PHY_CONTROL);
    /* Match the SoC firmware's cold-start POR sequence. Firmware on this
     * board leaves the common block in reset, so a warm handoff alone is
     * insufficient. Make the host PHY awake before any controller MMIO. */
    HWREG32(USB_PHY_CONTROL) = (phy & ~(USB_PHY_20MHZ | USB_UTMI_RELEASE)) | USB_PHY_RESET;
    rt_hw_dmb();
    rt_thread_mdelay(1);
    HWREG32(USB_PHY_CONTROL) =
        (phy & ~(USB_PHY_20MHZ | USB_PHY_RESET | USB_UTMI_RELEASE)) | (1U << 30);
    rt_hw_dmb();
    rt_thread_mdelay(2);
    HWREG32(USB_PHY_CONTROL) &= ~(1U << 28);
    rt_hw_dmb();
    rt_thread_mdelay(1);
    /* Complete the host cold-start flow from ls2k300/spl.c. These PHY bridge
     * transactions and bits 2/4 are part of that vendor sequence; they are
     * not inferred from the public manual's reserved-field descriptions. */
    HWREG32(USB_PHY_CONTROL) |= 7U;
    rt_hw_dmb();
    rt_kprintf("2K0302 USB: configuring host PHY address 0x18\n");
    error = usb_phy_transaction(0x03000018U);
    if (error != RT_EOK)
    {
        return error;
    }
    HWREG32(USB_PHY_DATA) |= 4U;
    rt_hw_dmb();
    error = usb_phy_transaction(0x07000018U);
    if (error != RT_EOK)
    {
        return error;
    }
    /* Retain the vendor handoff: bit 28 clear lets EHCI control suspend.
     * Forcing both bits 28/30 high stops controller MMIO on X2K032Z0. */
    HWREG32(USB_PHY_CONTROL) |= (1U << 4);
    rt_hw_dmb();
    HWREG32(CHIP_RESET_CONTROL) = resets | USB_MODULE_MASK;
    rt_hw_dmb();
    rt_thread_mdelay(1);

    rt_kprintf("2K0302 USB: clocks %08x -> %08x reset %08x -> %08x phy %08x -> %08x\n",
               clocks, HWREG32(CHIP_CLOCK_CONTROL), resets, HWREG32(CHIP_RESET_CONTROL),
               phy, HWREG32(USB_PHY_CONTROL));
    if ((HWREG32(CHIP_CLOCK_CONTROL) & USB_MODULE_MASK) != USB_MODULE_MASK ||
        (HWREG32(CHIP_RESET_CONTROL) & USB_MODULE_MASK) != USB_MODULE_MASK ||
        !(HWREG32(PLL_CLOCK_ENABLE) & USB_SOURCE_ENABLE) ||
        (HWREG32(USB_PHY_CONTROL) &
         (USB_PHY_20MHZ | USB_PHY_RESET | USB_UTMI_RELEASE |
          USB_HOST_SUSPEND | USB_HOST_OVERRIDE)) !=
            (USB_UTMI_RELEASE | USB_HOST_SUSPEND))
    {
        return -RT_EIO;
    }

    return RT_EOK;
}
#endif

void rt_hw_cpu_reset(void)
{
    rt_kprintf("REBOOT: 2K0302 SoC software reset\n");
    rt_thread_mdelay(20);
    rt_hw_interrupt_disable();
    /* WD_EN bit 0 is OS_RST; bit 1 enables the watchdog timer. */
    HWREG32(CHIP_CLOCK_CONTROL) |= 1U << 13;
    HWREG32(CHIP_RESET_CONTROL) |= 1U << 13;
    rt_hw_dmb();
    HWREG32(SOC_SOFTWARE_RESET) |= 1U;
    rt_hw_dmb();
    for (;;)
    {
        __asm__ volatile("idle 0");
    }
}

#ifdef RT_USING_FINSH
MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_reset, reboot, Restart the physical 2K0302 through SoC OS_RST);
#endif
#endif
