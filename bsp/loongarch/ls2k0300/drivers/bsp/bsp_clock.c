/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               derive peripheral clocks from firmware
 */

/*
 * Description:
 *   LS2K0300 development BSP: bsp clock.
 */

#include "ls2k_io.h"
#include "ls2k0300.h"

unsigned long ls2k_dev_apb_clock(void)
{
    return ls2k0300_apb_clock(
        readl(PHYS_TO_UNCACHED(LS2K0300_CLOCK_SAMPLE)), readl(PHYS_TO_UNCACHED(LS2K0300_DDR_PLL0)),
        readl(PHYS_TO_UNCACHED(LS2K0300_DDR_PLL1)), readl(PHYS_TO_UNCACHED(LS2K0300_CLOCK_SCALE)));
}
