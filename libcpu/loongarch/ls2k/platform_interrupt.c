/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#ifndef RT_USING_NANO
#include <drivers/dev_pin.h>
#endif

#include "platform_interrupt.h"
#include "cpuport.h"

static struct rt_spinlock irq_lock;
static struct rt_irq_desc irq_table[MAX_INTR];
static rt_uint64_t enabled_irqs;
static rt_uint32_t irq_polarity[2], irq_edge[2];

/**
 * @brief    Mask one LS2K peripheral interrupt source.
 *
 * @note     Invalid source numbers are ignored. The shared enable mask and hardware
 *           register update are protected against interrupts and concurrent CPUs.
 *
 * @param    irq is the controller source number in the range [0, MAX_INTR).
 */
void rt_hw_interrupt_mask(int irq)
{
    rt_base_t level;

    if (irq < 0 || irq >= MAX_INTR)
        return;
    level = rt_spin_lock_irqsave(&irq_lock);
    enabled_irqs &= ~(1ULL << irq);
    /* SET/CLR registers are write-one operations, never read-modify-write. */
    HWREG32(LS2K_INTENCLR_REG(irq)) = 1U << (irq % 32);
    rt_hw_dmb();
    rt_spin_unlock_irqrestore(&irq_lock, level);
}

/**
 * @brief    Unmask one LS2K peripheral interrupt source.
 *
 * @note     Invalid source numbers are ignored. The shared enable mask and hardware
 *           register update are protected against interrupts and concurrent CPUs.
 *
 * @param    irq is the controller source number in the range [0, MAX_INTR).
 */
void rt_hw_interrupt_umask(int irq)
{
    rt_base_t level;

    if (irq < 0 || irq >= MAX_INTR)
        return;
    level = rt_spin_lock_irqsave(&irq_lock);
    enabled_irqs |= 1ULL << irq;
    HWREG32(LS2K_INTENSET_REG(irq)) = 1U << (irq % 32);
    rt_hw_dmb();
    rt_spin_unlock_irqrestore(&irq_lock, level);
}

#ifndef RT_USING_NANO
/**
 * @brief    Set the trigger polarity and edge mode of a LIOINTC source.
 *
 * @note     Invalid source numbers or modes are ignored. Both register shadows and
 *           hardware updates are serialized by the controller lock.
 *
 * @param    irq is the controller source number in the range [0, MAX_INTR).
 *
 * @param    mode is PIN_IRQ_MODE_RISING, PIN_IRQ_MODE_FALLING,
 *           PIN_IRQ_MODE_HIGH_LEVEL or PIN_IRQ_MODE_LOW_LEVEL.
 */
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
    level = rt_spin_lock_irqsave(&irq_lock);
    polarity = irq_polarity[irq / 32] & ~bit;
    edge = irq_edge[irq / 32] & ~bit;
    
    if (mode == PIN_IRQ_MODE_LOW_LEVEL || mode == PIN_IRQ_MODE_FALLING)
        polarity |= bit;
    
    if (mode == PIN_IRQ_MODE_RISING || mode == PIN_IRQ_MODE_FALLING)
        edge |= bit;
    
    irq_polarity[irq / 32] = polarity;
    irq_edge[irq / 32] = edge;
    
    HWREG32(LS2K_INTPOL_REG(irq)) = polarity;
    HWREG32(LS2K_INTEDGE_REG(irq)) = edge;
    
    rt_hw_dmb();
    rt_spin_unlock_irqrestore(&irq_lock, level);
}

#endif

/**
 * @brief    Replace an LS2K interrupt handler and its callback argument.
 *
 * @note     Registration does not unmask the source. A handler already copied by
 *           another CPU may still run; the caller owns callback-argument lifetime.
 *
 * @param    vector is the controller source number in the range [0, MAX_INTR).
 *
 * @param    handler is the new callback, or RT_NULL to clear it.
 *
 * @param    parameter is the argument passed to the callback.
 *
 * @param    name is the optional name used by interrupt diagnostics.
 *
 * @return   The previous handler, or RT_NULL for an invalid vector or empty slot.
 */
rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
                                        void *parameter, const char *name)
{
    rt_isr_handler_t previous = RT_NULL;

    if (vector >= 0 && vector < MAX_INTR)
    {
        rt_base_t level = rt_spin_lock_irqsave(&irq_lock);
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
        rt_spin_unlock_irqrestore(&irq_lock, level);
    }
    return previous;
}

/**
 * @brief    Dispatch pending LS2K peripheral interrupts routed to CPU 0.
 *
 * @note     Runs in interrupt context. Edge status is acknowledged before the
 *           callback; level-triggered devices must clear their own pending condition.
 *           Callbacks run outside the controller lock and may mask their source.
 */
void platform_generic_irq(void)
{
    rt_uint64_t pending = HWREG32(LS2K_COREISR_REG0(0));

    pending |= (rt_uint64_t)HWREG32(LS2K_COREISR_REG1(0)) << 32;
    while (pending)
    {
        unsigned int irq = __builtin_ctzll(pending);
        rt_isr_handler_t handler;
        void *parameter;
        rt_base_t level = rt_spin_lock_irqsave(&irq_lock);

        pending &= pending - 1;
        
        if (!(enabled_irqs & (1ULL << irq)))
        {
            rt_spin_unlock_irqrestore(&irq_lock, level);
            continue;
        }
        
        if (irq_edge[irq / 32] & (1U << (irq % 32)))
        {
            /* Clear the latched edge before dispatch; CLR also masks it. */
            HWREG32(LS2K_INTENCLR_REG(irq)) = 1U << (irq % 32);
            HWREG32(LS2K_INTENSET_REG(irq)) = 1U << (irq % 32);
            rt_hw_dmb();
        }
        
        handler = irq_table[irq].handler;
        parameter = irq_table[irq].param;
#ifdef RT_USING_INTERRUPT_INFO
        if (handler)
            irq_table[irq].counter++;
#endif
        rt_spin_unlock_irqrestore(&irq_lock, level);
        
        /* Device handlers may mask their IRQ or acquire their own locks. */
        if (handler)
            handler(irq, parameter);
        else
            rt_hw_interrupt_mask(irq);
    }
}

/**
 * @brief    Initialize the LS2K controller and route its sources to CPU 0.
 *
 * @note     Called once by the boot CPU with peripheral interrupts quiesced.
 *           All sources start masked, active-high and level-triggered; board hooks
 *           prepare the interrupt route before the controller registers are written.
 */
void rt_hw_interrupt_init(void)
{
    unsigned int irq;

    rt_spin_lock_init(&irq_lock);
    ls2k_interrupt_prepare();
    enabled_irqs = 0;
    
    rt_memset(irq_polarity, 0, sizeof(irq_polarity));
    rt_memset(irq_edge, 0, sizeof(irq_edge));
    rt_memset(irq_table, 0, sizeof(irq_table));
    
    for (irq = 0; irq < MAX_INTR; irq++)
        HWREG8(LS2K_IRQ_ROUTE_REG(irq)) = LS2K_LIO_ROUTE_VALUE;
    
    for (irq = 0; irq < MAX_INTR; irq += 32)
    {
        HWREG32(LS2K_INTENCLR_REG(irq)) = 0xffffffff;
        HWREG32(LS2K_INTPOL_REG(irq)) = 0; /* active high */
        HWREG32(LS2K_INTEDGE_REG(irq)) = 0; /* level triggered */
#if LS2K_LIO_HAS_BOUNCE
        HWREG32(LS2K_BOUNCE_REG(irq)) = 0;
        HWREG32(LS2K_INTAUTO_REG(irq)) = 0;
#endif
    }
}
