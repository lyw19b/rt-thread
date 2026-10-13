/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <drivers/dev_pin.h>
#include "drv_interrupt.h"
#include "cpuport.h"

static struct rt_irq_desc irq_table[MAX_INTR];
static rt_uint64_t enabled_irqs;

void rt_hw_interrupt_mask(int irq)
{
    rt_base_t level;

    if (irq < 0 || irq >= MAX_INTR)
        return;
    level = rt_hw_interrupt_disable();
    enabled_irqs &= ~(1ULL << irq);
    /* SET/CLR registers are write-one operations, never read-modify-write. */
    HWREG32(LS2K_INTENCLR_REG(irq)) = 1U << (irq % 32);
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);
}

void rt_hw_interrupt_umask(int irq)
{
    rt_base_t level;

    if (irq < 0 || irq >= MAX_INTR)
        return;
    level = rt_hw_interrupt_disable();
    enabled_irqs |= 1ULL << irq;
    HWREG32(LS2K_INTENSET_REG(irq)) = 1U << (irq % 32);
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);
}

void liointc_set_irq_mode(int irq, int mode)
{
    rt_base_t level;
    rt_uint32_t bit, polarity, edge;

    if (irq < 0 || irq >= MAX_INTR)
        return;
    if (mode != PIN_IRQ_MODE_RISING && mode != PIN_IRQ_MODE_FALLING &&
        mode != PIN_IRQ_MODE_HIGH_LEVEL && mode != PIN_IRQ_MODE_LOW_LEVEL)
        return;
    bit = 1U << (irq % 32);
    level = rt_hw_interrupt_disable();
    polarity = HWREG32(LS2K_INTPOL_REG(irq)) & ~bit;
    edge = HWREG32(LS2K_INTEDGE_REG(irq)) & ~bit;
    if (mode == PIN_IRQ_MODE_LOW_LEVEL || mode == PIN_IRQ_MODE_FALLING)
        polarity |= bit;
    if (mode == PIN_IRQ_MODE_RISING || mode == PIN_IRQ_MODE_FALLING)
        edge |= bit;
    HWREG32(LS2K_INTPOL_REG(irq)) = polarity;
    HWREG32(LS2K_INTEDGE_REG(irq)) = edge;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);
}

rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
                                        void *parameter, const char *name)
{
    rt_isr_handler_t previous = RT_NULL;

    if (vector >= 0 && vector < MAX_INTR)
    {
        rt_base_t level = rt_hw_interrupt_disable();
        previous = irq_table[vector].handler;
        irq_table[vector].handler = handler;
        irq_table[vector].param = parameter;
#ifdef RT_USING_INTERRUPT_INFO
        rt_strncpy(irq_table[vector].name, name ? name : "", RT_NAME_MAX - 1);
        irq_table[vector].name[RT_NAME_MAX - 1] = '\0';
        irq_table[vector].counter = 0;
#else
        RT_UNUSED(name);
#endif
        rt_hw_dmb();
    rt_hw_interrupt_enable(level);
    }
    return previous;
}

void platform_generic_irq(void)
{
    rt_uint64_t pending = HWREG32(LS2K_COREISR_REG0(0));

    pending |= (rt_uint64_t)HWREG32(LS2K_COREISR_REG1(0)) << 32;
    pending &= enabled_irqs;
    while (pending)
    {
        unsigned int irq = __builtin_ctzll(pending);
        struct rt_irq_desc *desc = &irq_table[irq];

        pending &= pending - 1;
        if (!(enabled_irqs & (1ULL << irq)))
            continue;
        /* Do not unconditionally unmask after dispatch: a device ISR may
         * intentionally leave its interrupt disabled for deferred recovery. */
        if (desc->handler)
        {
#ifdef RT_USING_INTERRUPT_INFO
            desc->counter++;
#endif
            desc->handler(irq, desc->param);
        }
        else
            rt_hw_interrupt_mask(irq);
    }
}

void rt_hw_interrupt_init(void)
{
    unsigned int irq;

    enabled_irqs = 0;
    rt_memset(irq_table, 0, sizeof(irq_table));
    for (irq = 0; irq < MAX_INTR; irq++)
        HWREG8(LS2K_IRQ_ROUTE_REG(irq)) = LIOINTC_COREx_INTy(0, 0);
    for (irq = 0; irq < MAX_INTR; irq += 32)
    {
        HWREG32(LS2K_INTENCLR_REG(irq)) = 0xffffffff;
        HWREG32(LS2K_INTPOL_REG(irq)) = 0; /* active high */
        HWREG32(LS2K_INTEDGE_REG(irq)) = 0; /* level triggered */
        HWREG32(LS2K_BOUNCE_REG(irq)) = 0;
        HWREG32(LS2K_INTAUTO_REG(irq)) = 0;
    }
}
