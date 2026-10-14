/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#include <rtthread.h>

#include "stack.h"
#include <stddef.h>

RT_STATIC_ASSERT(exception_frame_size, sizeof(struct pt_regs) == PT_SIZE);
RT_STATIC_ASSERT(exception_fpu_offset, offsetof(struct pt_regs, f) == PT_FPR0);
RT_STATIC_ASSERT(switch_fpu_offset, offsetof(struct rt_hw_switch_frame, f) == RT_THREAD_FPR24);

/**
 * @brief    Build the initial kernel-thread context for the first scheduler restore.
 *
 * @note     Aligns the stack to 16 bytes. The switch frame passes entry, argument
 *           and exit routine in s0-s2 to _rt_thread_entry; it is not a user-mode frame.
 *
 * @param    tentry is the thread entry function.
 *
 * @param    parameter is the argument passed to tentry.
 *
 * @param    stack_addr is the supplied top of the downward-growing stack.
 *
 * @param    texit is the exit routine called when tentry returns.
 *
 * @return   The saved stack pointer to store in the thread control block.
 */
rt_uint8_t *rt_hw_stack_init(void *tentry, void *parameter, rt_uint8_t *stack_addr, void *texit)
{
    rt_ubase_t top = RT_ALIGN_DOWN((rt_ubase_t)stack_addr, 16);
    rt_hw_switch_frame_t frame = (void *)(top - sizeof(*frame));
    extern void _rt_thread_entry(void);

    RT_ASSERT(sizeof(*frame) == PT_SWITCH_FRAME_SIZE);
    rt_memset(frame, 0, sizeof(*frame));
    
    frame->r_ra = (rt_ubase_t)_rt_thread_entry;
    frame->r_s0 = (rt_ubase_t)tentry;
    frame->r_s1 = (rt_ubase_t)parameter;
    frame->r_s2 = (rt_ubase_t)texit;
    frame->r_prmd = DEFAULT_THREAD_PRMD;
    frame->r_crmd = DEFAULT_THREAD_CRMD;
    return (void *)frame;
}
