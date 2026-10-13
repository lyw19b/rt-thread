/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rthw.h>
#include "ls2k.h"
#include "ls2k_uart.h"
#include "ls2k0300.h"
#include "drv_interrupt.h"

void ls2k_interrupt_prepare(void)
{
    /* Firmware can leave EXTIOI enabled. Use only the traditional controller
     * so an interrupt cannot arrive through both routes. Preserve pin mux. */
    HWREG32(LS2K_UNCACHED(LS2K0300_CHIP_CONFIG0)) &= ~(1U << 19);
    rt_hw_dmb();
}

const struct ls2k_uart_config *ls2k_uart_get_config(rt_size_t *count)
{
    static struct ls2k_uart_config port;

    port.base = LS2K_UNCACHED(LS2K0300_UART_BASE(BSP_UART_INDEX));
    port.irq = ls2k0300_uart_irq(BSP_UART_INDEX);
    port.name = "uart";
    port.fifo_control = 0x06; /* FCR bit 0 is reserved on 2K0300. */
    port.clock = BSP_UART_CLOCK_HZ;
    if (!port.clock)
        port.clock = ls2k0300_apb_clock(
            HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SAMPLE)),
            HWREG32(LS2K_UNCACHED(LS2K0300_DDR_PLL0)),
            HWREG32(LS2K_UNCACHED(LS2K0300_DDR_PLL1)),
            HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SCALE)));
    *count = 1;
    return &port;
}

rt_err_t ls2k_gmac_platform_init(unsigned int port, rt_ubase_t *base, int *irq)
{
    rt_base_t level;
    rt_uint32_t control;

    if (port > 1 || !base || !irq)
        return -RT_EINVAL;
    *base = LS2K_UNCACHED(LS2K0300_GMAC_BASE(port));
    *irq = LS2K0300_GMAC_IRQ(port);
    /* DMA buffers use low DDR and uncached CPU aliases. Force this port's
     * DMA requests to DDR regardless of firmware's coherency selection. */
    level = rt_hw_interrupt_disable();
    control = HWREG32(LS2K_UNCACHED(LS2K0300_CHIP_CONFIG3));
    control |= 1U << (2 + port);
    control &= ~(1U << (10 + port));
    HWREG32(LS2K_UNCACHED(LS2K0300_CHIP_CONFIG3)) = control;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

rt_ubase_t ls2k_gmac_dma_limit(void)
{
    /* Low DDR has identical CPU and DMA addresses. Do not silently accept
     * a future linker move into the high DDR / CCIO alias. */
    return LS2K0300_DMA_LIMIT;
}
