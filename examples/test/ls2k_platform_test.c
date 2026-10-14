/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
/* Host-side register tests. The runner provides a small RT-Thread/MMIO shim;
 * the interrupt, polling UART and 2K0300 resource code below is production code. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rtthread.h"

static unsigned int mmio[0x200000 / sizeof(unsigned int)];
static unsigned int interrupt_depth, barrier_count, dispatch_count, delay_count;

void *test_mmio(uintptr_t address)
{
    uintptr_t physical = address & 0x0000ffffffffffffUL;
#ifdef TEST_LS2K1000
    const uintptr_t base = 0x1fe00000;
#else
    const uintptr_t base = 0x16000000;
#endif
    assert(physical >= base && physical < base + sizeof(mmio));
    return (unsigned char *)mmio + physical - base;
}

rt_base_t rt_hw_interrupt_disable(void)
{
    return interrupt_depth++;
}

void rt_hw_interrupt_enable(rt_base_t level)
{
    assert(interrupt_depth > 0);
    interrupt_depth = level;
}

void rt_hw_dmb(void)
{
    barrier_count++;
}

void rt_thread_mdelay(int ms)
{
    assert(ms == 1);
    delay_count++;
}

#include "../../libcpu/loongarch/ls2k/drv_interrupt.c"
#define RT_USING_NANO
#include "../../bsp/loongarch/common/drv_uart.c"
#ifdef TEST_LS2K1000
#include "../../bsp/loongarch/ls2kdev/drivers/drv_uart_config.c"
#else
#include "../../bsp/loongarch/ls2k0300/drivers/drv_platform.c"
#endif

static void _dispatch(int irq, void *parameter)
{
    assert(parameter == &dispatch_count);
    dispatch_count++;
    rt_hw_interrupt_mask(irq);
}

static void _interrupt_test(void)
{
    unsigned int i;

    memset(mmio, 0xa5, sizeof(mmio));
    rt_hw_interrupt_init();
    assert(enabled_irqs == 0);
    for (i = 0; i < 64; i++)
        assert(HWREG8(LS2K_IRQ_ROUTE_REG(i)) == LS2K_LIO_ROUTE_VALUE);
    for (i = 0; i < 64; i += 32)
    {
        assert(HWREG32(LS2K_INTENCLR_REG(i)) == UINT32_MAX);
        assert(HWREG32(LS2K_INTPOL_REG(i)) == 0);
        assert(HWREG32(LS2K_INTEDGE_REG(i)) == 0);
#if !LS2K_LIO_HAS_BOUNCE
        assert(HWREG32(LS2K_BOUNCE_REG(i)) == 0xa5a5a5a5);
        assert(HWREG32(LS2K_INTAUTO_REG(i)) == 0xa5a5a5a5);
#endif
    }
    i = barrier_count;
    rt_hw_interrupt_mask(-1);
    rt_hw_interrupt_umask(64);
    assert(barrier_count == i);

    liointc_set_irq_mode(49, PIN_IRQ_MODE_FALLING);
    liointc_set_irq_mode(50, PIN_IRQ_MODE_LOW_LEVEL);
    assert(HWREG32(LS2K_INTPOL_REG(49)) == ((1U << 17) | (1U << 18)));
    assert(HWREG32(LS2K_INTEDGE_REG(49)) == (1U << 17));
    assert(rt_hw_interrupt_install(49, _dispatch, &dispatch_count, "test") == RT_NULL);
    assert(rt_hw_interrupt_install(49, _dispatch, &dispatch_count, "test") == _dispatch);
    rt_hw_interrupt_umask(49);
    HWREG32(LS2K_COREISR_REG0(0)) = 0;
    HWREG32(LS2K_COREISR_REG1(0)) = (1U << 17) | (1U << 18);
    platform_generic_irq();
    assert(dispatch_count == 1 && !(enabled_irqs & (1ULL << 49)));
    platform_generic_irq();
    assert(dispatch_count == 1); /* ISR's mask must survive dispatch. */
    rt_hw_interrupt_umask(50);  /* Unhandled IRQ is masked, not dispatched. */
    platform_generic_irq();
    assert(dispatch_count == 1 && enabled_irqs == 0);
    assert(interrupt_depth == 0);
    puts("LIOINTC routing, write-one masks, edge acknowledgement and deferred masking: PASS");
}

#ifndef TEST_LS2K1000
static void _clock_and_dma_test(void)
{
    const uint32_t scale = 7U << 16;
    const uint32_t pll = 4 | 0x80 | (3U << 8) | (50U << 15);
    unsigned int port;

    assert(ls2k0300_apb_clock(0, 0, 0, scale) == 100000000);
    assert(ls2k0300_apb_clock(2, 0, 0, scale) == 150000000);
    assert(ls2k0300_apb_clock(6, 0, 0, scale) == 120000000);
    assert(ls2k0300_apb_clock(4, 0, 0, scale) == 120000000);
    assert(ls2k0300_apb_clock(4, pll, 10U << 8, scale) == 200000000);
    assert(ls2k0300_apb_clock(4, pll, 10U << 8, 9U << 16) == 100000000);
    assert(ls2k0300_apb_clock(4, pll, 10U << 8, 3U << 16) == 100000000);
    assert(ls2k0300_apb_clock(4, pll, 0, scale) == 0);
    assert(ls2k0300_apb_clock(4, pll & ~0x80, 10U << 8, scale) == 0);
    assert(ls2k0300_apb_clock(4, pll | 0x10, 0, scale) == 120000000);
    for (port = 0; port < 10; port++)
    {
        unsigned int expected = port < 2 ? port : (port < 6 ? 2 : 3);
        assert(ls2k0300_uart_irq(port) == expected);
        assert(LS2K0300_UART_BASE(port) == 0x16100000 + port * 1024);
    }
    for (port = 0; port < 2; port++)
    {
        rt_ubase_t base;
        int irq;
        uint32_t expected = UINT32_MAX & ~(1U << (10 + port));
        HWREG32(LS2K_UNCACHED(LS2K0300_CHIP_CONFIG3)) = UINT32_MAX;
        assert(ls2k_gmac_platform_init(port, &base, &irq) == RT_EOK);
        assert((base & 0xffffffff) == 0x16020000 + port * 0x10000);
        assert(irq == (int)(49 + port));
        assert(HWREG32(LS2K_UNCACHED(LS2K0300_CHIP_CONFIG3)) == expected);
        assert(ls2k_gmac_platform_init(2, &base, &irq) == -RT_EINVAL);
        assert(ls2k_gmac_platform_init(port, RT_NULL, &irq) == -RT_EINVAL);
    }
    assert(ls2k_gmac_dma_limit() == 0x10000000);
    assert(interrupt_depth == 0);
    puts("2K0300 clock modes/dividers, UART IRQs and per-port DMA routing: PASS");
}
#endif

static void _uart_test(void)
{
    unsigned int i;

    memset(mmio, 0, sizeof(mmio));
#ifndef TEST_LS2K1000
    HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SCALE)) = 7U << 16;
#endif
    rt_hw_uart_init();
    assert(port_count >= 1);
    assert(_read(ports, UART_FCR) == ports->fifo_control);
    assert(_read(ports, UART_LCR) == 3 && _read(ports, UART_IER) == 0);
    assert(_set_format(ports, 0, 3) == -RT_EINVAL);
    assert(_set_format(ports, 1, 3) == -RT_EINVAL);
    _write(ports, UART_IER, 5);
    assert(_set_format(ports, 115200, 0x1b) == RT_EOK);
    assert(_read(ports, UART_LCR) == 0x1b && _read(ports, UART_IER) == 5);
    _write(ports, UART_LSR, UART_TX | UART_RX);
    for (i = 0; i < 100000; i++)
    {
        char expected = 'a' + i % 26;
        _putchar(ports, expected);
        assert(rt_hw_console_getchar() == expected);
    }
    rt_hw_console_output("test\n");
    assert(_read(ports, UART_DATA) == '\n');
    _write(ports, UART_LSR, 0);
    assert(rt_hw_console_getchar() == (char)-1 && delay_count == 1);
    assert(interrupt_depth == 0);
    puts("Polling UART baud validation, DLAB exclusion and 100000 character transfers: PASS");
}

int main(void)
{
    _interrupt_test();
#ifndef TEST_LS2K1000
    _clock_and_dma_test();
#endif
    _uart_test();
    return 0;
}
