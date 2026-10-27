/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: drv laxpu.
 */

#include <rtthread.h>
#if defined(BSP_USING_LAXPU) && !defined(BSP_USING_LS2K0300_QEMU) && !defined(BSP_USING_HUMMINGBIRD)
#include <laxpu_board.h>
#include "ls2k.h"

static struct laxpu_board accelerator = {
    .device = { .name = "mix0302", .dma_limit = 0x10000000UL },
    .registers = (void *)LS2K_UNCACHED(0x1610c000UL),
    .localio = (void *)LS2K_UNCACHED(0x14000000UL),
    .frame_port = (void *)LS2K_UNCACHED(0x16090000UL),
    .irq = 63,
};

static int accelerator_init(void)
{
#ifdef LAXPU_USING_NOVAE
    return laxpu_novae_register(&accelerator);
#else
    return laxpu_nebulae_register(&accelerator);
#endif
}

INIT_DEVICE_EXPORT(accelerator_init);
#endif

#if defined(BSP_USING_LAXPU) && defined(LAXPU_USING_DISPLAY)
#include "ls2k.h"
#include <ls2k0300_display.h>

#ifdef BSP_USING_VANGUARD_TOUCH
int ls2k0300_vanguard_touch_init(void);
#endif
#ifdef BSP_USING_HUMMINGBIRD
int ls2k0300_hummingbird_panel_init(void);
#endif

static int panel_init(void)
{
#ifdef BSP_USING_HUMMINGBIRD
    return ls2k0300_hummingbird_panel_init();
#else
    int error = ls2k0300_lcd_register((void *)LS2K_UNCACHED(0x16090000UL), 51);

    if (error)
    {
        return error;
    }

#ifdef BSP_USING_VANGUARD_TOUCH
    return ls2k0300_vanguard_touch_init();
#elif defined(BSP_USING_LS2K0300_QEMU)
    /* The standalone SoC machine has no external GT911 panel. */
    return RT_EOK;
#else
    /* The QEMU panel uses I2C0 and a dedicated virtual interrupt input. */
    error = ls2k0300_i2c_register((void *)LS2K_UNCACHED(0x16108000UL), 50000000, "i2c0");
    if (error)
    {
        return error;
    }

    return laxpu_gt911_register("i2c0", 0x5d, 62);
#endif
#endif
}

INIT_DEVICE_EXPORT(panel_init);
#endif
