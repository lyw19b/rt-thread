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

/**
 * This function will initialize thread stack, we assuming
 * when scheduler restore this new thread, context will restore
 * an entry to user first application
 *
 * @param tentry the entry of thread
 * @param parameter the parameter of entry
 * @param stack_addr the beginning stack address
 * @param texit the function will be called when thread exit
 *
 * @return stack address
 */

rt_uint8_t *rt_hw_stack_init(void *tentry, void *parameter, rt_uint8_t *stack_addr, void *texit)
{
    rt_ubase_t *tsp;
    struct pt_regs *pt;

    /* Get stack aligned */
    tsp = (rt_ubase_t *)RT_ALIGN_DOWN((rt_ubase_t)stack_addr, 16);
    tsp = (void *)tsp - sizeof(struct pt_regs);
    pt =  (struct pt_regs*)tsp;
    rt_memset(pt, 0, sizeof(*pt));

    pt->r_t0 = (rt_ubase_t)tentry;
    pt->r_t1 = (rt_ubase_t)parameter;
    pt->r_t2 = (rt_ubase_t)texit;

    rt_hw_switch_frame_t frame = (rt_hw_switch_frame_t)
        ((rt_ubase_t)tsp - sizeof(struct rt_hw_switch_frame));

    RT_ASSERT(sizeof(struct rt_hw_switch_frame) == PT_SWITCH_FRAME_SIZE);

    rt_memset(frame, 0, sizeof(struct rt_hw_switch_frame));

    extern void _rt_thread_entry(void);

    frame->r_ra = (rt_ubase_t)_rt_thread_entry;
    frame->r_prmd = DEFAULT_THREAD_PRMD;
    frame->r_crmd = DEFAULT_THREAD_CRMD;

    return (void *)frame;
}
