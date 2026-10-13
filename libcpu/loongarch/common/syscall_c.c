/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include "stack.h"
#ifdef RT_USING_SMART
#include <lwp_arch.h>
#include <lwp_ipc.h>
#include <lwp_syscall.h>
#include <lwp_internal.h>

typedef rt_ubase_t (*loongarch_syscall_t)(rt_ubase_t, rt_ubase_t, rt_ubase_t,
    rt_ubase_t, rt_ubase_t, rt_ubase_t, rt_ubase_t);

void rt_dispatch_syscall(struct pt_regs *regs)
{
    loongarch_syscall_t call;
    rt_thread_t thread = rt_thread_self();

    if (!(regs->r_prmd & 3))
    {
        extern rt_base_t loongarch_kernel_syscall(struct pt_regs *regs);
        rt_hw_interrupt_enable(4);
        regs->r_a0 = loongarch_kernel_syscall(regs);
        rt_hw_interrupt_disable();
        regs->r_era += 4;
        return;
    }
    thread->user_ctx.ctx = regs;
    thread->thread_idr = (void *)regs->r_tp;
    if (regs->r_a7 == 0xfe)
    {
        arch_signal_ucontext_restore(regs->r_sp, (rt_base_t)regs);
        return;
    }
    call = (loongarch_syscall_t)lwp_get_sys_api(regs->r_a7);
    /* Keep ERA at the syscall until fork has copied the parent's frame. */
    rt_hw_interrupt_enable(4);
    regs->r_a0 = call ? call(regs->r_a0, regs->r_a1, regs->r_a2,
        regs->r_a3, regs->r_a4, regs->r_a5, regs->r_a6) : -ENOSYS;
    rt_hw_interrupt_disable();
    regs->r_tp = (rt_ubase_t)thread->thread_idr;
    regs->r_era += 4;
}
#else
void rt_dispatch_syscall(struct pt_regs *regs)
{
#ifdef RT_USING_DFS
    extern rt_base_t loongarch_kernel_syscall(struct pt_regs *regs);
    rt_hw_interrupt_enable(4);
    regs->r_a0 = loongarch_kernel_syscall(regs);
    rt_hw_interrupt_disable();
#else
    regs->r_a0 = -RT_ENOSYS;
#endif
    regs->r_era += 4;
}
#endif
