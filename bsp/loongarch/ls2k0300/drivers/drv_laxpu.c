/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: GPL-2.0
 */
#include <rtthread.h>
#ifdef BSP_USING_LAXPU
#include <laxpu_board.h>
#include "ls2k.h"

static struct laxpu_board accelerator =
{
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
#include <ls2k0300_display.h>

static int panel_init(void)
{
    int error = ls2k0300_lcd_register((void *)LS2K_UNCACHED(0x16090000UL), 51);
    if (error)
        return error;
    /* The QEMU panel uses I2C0 and a dedicated virtual interrupt input. */
    error = ls2k0300_i2c_register((void *)LS2K_UNCACHED(0x16108000UL),
                                50000000, "i2c0");
    if (error)
        return error;
    return laxpu_gt911_register("i2c0", 0x5d, 62);
}
INIT_DEVICE_EXPORT(panel_init);
#endif
