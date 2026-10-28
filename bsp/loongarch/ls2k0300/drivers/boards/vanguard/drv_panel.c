/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add physical Vanguard LCD adapter
 */

/*
 * Description:
 *   Vanguard RGB LCD and GPIO43 backlight, separate from Hummingbird wiring.
 *   Geometry matches the live ATK-MD0700R-1024x600 panel-timing. Preserve the
 *   firmware pixel PLL; it also feeds GMAC and must not be retuned blindly.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"

#if defined(BSP_USING_VANGUARD_TOUCH) && !defined(BSP_USING_LS2K0300_QEMU)
#include <ls2k0300_display.h>

#define PANEL_BASE       0x16090000UL
#define PINMUX_BASE      0x16000490UL
#define GPIO_BASE        0x16104000UL
#define BACKLIGHT_PIN    43U
#define PIXEL_PLL        0x16000410UL
#define CLOCK_ENABLE     0x16000424UL
#define CHIP_CONFIG3     0x1600010cUL
#define DC_COHERENT      ((1U << 12) | (1U << 4))

int ls2k0300_vanguard_touch_init(void);

int ls2k0300_vanguard_panel_init(void)
{
    static const struct ls2k0300_lcd_timing timing =
    {
        .hfront = 160, .hsync = 20, .hback = 140,
        .vfront = 12, .vsync = 3, .vback = 20,
        .dvo_config = 0x301U,
    };
    rt_uint32_t pll = HWREG32(LS2K_UNCACHED(PIXEL_PLL));
    rt_base_t level;
    int error;

    if ((pll & 0xb1U) != 0x81U)
    {
        rt_kprintf("Vanguard LCD: firmware pixel PLL is not ready (%08x)\n", pll);
        return -RT_EIO;
    }

    HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x1240)) &= ~(1U << 8);
    rt_hw_dmb();
    level = rt_hw_interrupt_disable();

    /* DVO uses G0..27, GPIO43 is the independent active-high backlight.
     * Keep the remaining mux fields and electrical drive strength intact. */
    HWREG32(LS2K_UNCACHED(PINMUX_BASE)) = 0xffffffffU;
    HWREG32(LS2K_UNCACHED(PINMUX_BASE + 4)) |= 0x00ffffffU;
    HWREG32(LS2K_UNCACHED(PINMUX_BASE + 8)) &= ~(3U << 22);
    HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) |= 1U << 6;

    /* vmalloc_user supplies cached framebuffers. LA264 cache helpers are
     * barriers only, so DC must read through the coherent interconnect. */
    HWREG32(LS2K_UNCACHED(CHIP_CONFIG3)) |= DC_COHERENT;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);

    error = ls2k0300_lcd_register_timing((void *)LS2K_UNCACHED(PANEL_BASE), 51, &timing);

    if (error)
    {
        return error;
    }

    HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x900 + BACKLIGHT_PIN)) = 1;
    HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x800 + BACKLIGHT_PIN)) = 0;
    rt_hw_dmb();
    rt_kprintf("Vanguard LCD: ATK 1024x600, firmware PLL=%08x, GPIO43 backlight on\n", pll);

    return ls2k0300_vanguard_touch_init();
}

#ifdef RT_USING_FINSH
static void vanguard_panel_status(void)
{
    rt_kprintf("Vanguard LCD: pll=%08x scale=%08x gate=%08x coherent=%08x\n",
               HWREG32(LS2K_UNCACHED(PIXEL_PLL)),
               HWREG32(LS2K_UNCACHED(0x16000420UL)),
               HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)),
               HWREG32(LS2K_UNCACHED(CHIP_CONFIG3)));
    rt_kprintf("LCD config=%08x dvo=%08x fb0=%08x fb1=%08x scan=%08x bl=%u dir=%u\n",
               HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x1240)),
               HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x13c0)),
               HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x1260)),
               HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x1580)),
               HWREG32(LS2K_UNCACHED(PANEL_BASE + 0x14c0)),
               HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x900 + BACKLIGHT_PIN)),
               HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x800 + BACKLIGHT_PIN)));
}
MSH_CMD_EXPORT(vanguard_panel_status, Show Vanguard LCD and backlight state);
#endif
#endif
