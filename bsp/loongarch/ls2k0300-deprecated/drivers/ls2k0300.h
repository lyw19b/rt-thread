/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K0300_H__
#define LS2K0300_H__

#include <stdint.h>

/* LS2K0300 Processor User Manual V1.1, sections 2, 3.2, 3.4 and 3.5. */
#define LS2K0300_CHIP_CONFIG0       0x16000100UL
#define LS2K0300_CHIP_CONFIG3       0x1600010cUL
#define LS2K0300_CLOCK_SAMPLE       0x16000140UL
#define LS2K0300_DDR_PLL0           0x16000408UL
#define LS2K0300_DDR_PLL1           0x1600040cUL
#define LS2K0300_CLOCK_SCALE        0x16000420UL
#define LS2K0300_UART_BASE(index)   (0x16100000UL + (index) * 0x400UL)
#define LS2K0300_GMAC_BASE(index)   (0x16020000UL + (index) * 0x10000UL)
#define LS2K0300_GMAC_IRQ(index)    (49 + (index))
#define LS2K0300_DMA_LIMIT          0x10000000UL

static inline unsigned int ls2k0300_uart_irq(unsigned int index)
{
    return index < 2 ? index : (index < 6 ? 2 : 3);
}

/* Read-only decoding: changing DDR PLL parameters would also change memory
 * and bus clocks. Return zero for invalid firmware programming. */
static inline uint32_t ls2k0300_apb_clock(uint32_t sample, uint32_t pll0,
                                        uint32_t pll1, uint32_t scale)
{
    unsigned int mode = (sample >> 1) & 3;
    unsigned int factor = ((scale >> 16) & 7) + 1;
    uint64_t rate;

    if (mode == 0)
        rate = 100000000;
    else if (mode == 1)
        rate = 150000000;
    else
    {
        rate = 120000000;
        if (mode == 2 && (pll0 & 4) && !(pll0 & 0x10))
        {
            unsigned int ref = (pll0 >> 8) & 0x7f;
            unsigned int loop = (pll0 >> 15) & 0x1ff;
            unsigned int out = (pll1 >> 8) & 0x7f;
            if (!ref || !loop || !out || !(pll0 & 0x80) || (pll0 & 0x20))
                return 0;
            rate = rate * loop / ((uint64_t)ref * out);
        }
    }
    rate = (scale & (1U << 19)) ? rate / factor : rate * factor / 8;
    return rate <= UINT32_MAX ? (uint32_t)rate : 0;
}

#endif
