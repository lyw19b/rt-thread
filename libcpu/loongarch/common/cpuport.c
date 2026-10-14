/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#include <rthw.h>
#include <rtthread.h>
#include "loongarch.h"

#ifndef RT_USING_SMP
/* Address of the first interrupted thread's saved stack-pointer slot. */
volatile rt_ubase_t rt_interrupt_from_thread = 0;

/* Address of the latest selected thread's saved stack-pointer slot. */
volatile rt_ubase_t rt_interrupt_to_thread = 0;

/* A nonzero value requests a context switch on exception return. */
volatile rt_ubase_t rt_thread_switch_interrupt_flag = 0;

/**
 * @brief    Defer a UP context switch until exception return.
 *
 * @note     The caller must mask interrupts. Repeated requests preserve the first
 *           outgoing context and replace only the selected incoming context.
 *
 * @param    from is the address of the outgoing thread saved stack-pointer slot.
 *
 * @param    to is the address of the incoming thread saved stack-pointer slot.
 *
 * @param    from_thread is the outgoing thread, unused by this UP implementation.
 *
 * @param    to_thread is the incoming thread, unused by this UP implementation.
 */
void rt_hw_context_switch_interrupt(rt_ubase_t from, rt_ubase_t to, 
                                    rt_thread_t from_thread, rt_thread_t to_thread)
{
    if (rt_thread_switch_interrupt_flag == 0)
        rt_interrupt_from_thread = from;

    rt_interrupt_to_thread = to;
    rt_thread_switch_interrupt_flag = 1;

    return;
}
#else
/**
 * @brief    Switch threads from the SMP interrupt-return scheduler.
 *
 * @note     The scheduler supplies IRQ and context-lock exclusion. The assembly
 *           switch saves its own frame, so context is not consumed here.
 *
 * @param    context is the exception frame, retained on the outgoing stack.
 *
 * @param    from is the address of the outgoing thread saved stack-pointer slot.
 *
 * @param    to is the address of the incoming thread saved stack-pointer slot.
 *
 * @param    to_thread is the thread whose scheduler and address-space state is restored.
 */
void rt_hw_context_switch_interrupt(void *context, rt_ubase_t from,
                                    rt_ubase_t to, rt_thread_t to_thread)
{
    RT_UNUSED(context);
    rt_hw_context_switch(from, to, to_thread);
}
#endif

/**
 * @brief    Read the hardware CPU ID used to index per-CPU state.
 *
 * @return   The local CPU ID, or zero in a UP build.
 */
int rt_hw_cpu_id(void)
{
#ifdef RT_USING_SMP
    return read_csr_cpuid() & 0x3ff;
#else
    return 0;
#endif
}

/**
 * @brief    Test the local interrupt-enable bit in CRMD.
 *
 * @return   RT_TRUE if local maskable interrupts are disabled; RT_FALSE otherwise.
 */
rt_bool_t rt_hw_interrupt_is_disabled(void)
{
    return !(csr_read32(LOONGARCH_CSR_CRMD) & CSR_CRMD_IE);
}

/**
 * @brief    Find the least significant set bit in a machine word.
 *
 * @param    value is the bitmap to inspect.
 *
 * @return   The one-based bit position, or zero when value is zero.
 */
unsigned long __rt_ffsl(unsigned long value)
{
    return __builtin_ffsl(value);
}
