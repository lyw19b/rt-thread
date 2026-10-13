/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include "loongarch.h"
#include <lwp_ipc.h>
#include <lwp_syscall.h>
#include <stddef.h>


#include <rthw.h>
#include <rtthread.h>
#include <lwp_internal.h>
#ifdef ARCH_MM_MMU

#define DBG_TAG "lwp.arch"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#include "stack.h"

#include <lwp_arch.h>
#include <lwp_user_mm.h>

extern size_t MMUTable[];



int arch_user_space_init(struct rt_lwp *lwp)
{
    rt_ubase_t *mmu_table;

    mmu_table = rt_hw_mmu_pgtbl_create();
    if (!mmu_table)
    {
        return -RT_ENOMEM;
    }

    lwp->end_heap = USER_HEAP_VADDR;
    lwp->aspace = rt_aspace_create(
        (void *)USER_VADDR_START, USER_VADDR_TOP - USER_VADDR_START, mmu_table);
    if (!lwp->aspace)
    {
        rt_hw_mmu_pgtbl_delete(mmu_table);
        return -RT_ENOMEM;
    }

    return 0;
}

void *arch_get_user_sp(void)
{
    /* user sp saved in interrupt context */
    rt_thread_t self = rt_thread_self();
    rt_uint8_t *stack_top = (rt_uint8_t *)self->stack_addr + self->stack_size;
    struct pt_regs *frame = (struct pt_regs *)(stack_top - sizeof(struct pt_regs));

    return (void *)frame->r_sp;
}

void *arch_kernel_mmu_table_get(void)
{
    return (void *)((char *)MMUTable);
}


void arch_user_space_free(struct rt_lwp *lwp)
{
    if (lwp)
    {
        RT_ASSERT(lwp->aspace);

        void *pgtbl = lwp->aspace->page_table;
        rt_aspace_delete(lwp->aspace);

        /* must be freed after aspace delete, pgtbl is required for unmap */
        rt_hw_mmu_pgtbl_delete(pgtbl);
        lwp->aspace = RT_NULL;
    }
    else
    {
        LOG_W("%s: NULL lwp as parameter", __func__);
        RT_ASSERT(0);
    }
}

#endif /* ARCH_MM_MMU */


/**
 * set exec context for fork/clone.
 */
int arch_set_thread_context(void (*exit)(void), void *new_thread_stack,
                            void *user_stack, void **thread_sp)
{
    struct pt_regs *regs = (struct pt_regs *)new_thread_stack - 1;
    struct rt_hw_switch_frame *frame = (struct rt_hw_switch_frame *)regs - 1;
    rt_thread_t thread = rt_container_of(thread_sp, struct rt_thread, sp);

    regs->r_sp = (rt_ubase_t)user_stack;
    regs->r_era += 4;
    regs->r_a0 = 0;
    regs->r_tp = (rt_ubase_t)thread->thread_idr;
    rt_memset(frame, 0, sizeof(*frame));
    frame->r_ra = (rt_ubase_t)exit;
    frame->r_crmd = 0xb0;
    frame->r_prmd = 4;
    *thread_sp = frame;
    return 0;
}


#define ALGIN_BYTES (16)

struct signal_ucontext
{
    rt_int64_t sigreturn;
    lwp_sigset_t save_sigmask;

    siginfo_t si;

    rt_align(16)
    struct pt_regs frame;
};


extern void loongarch_restore_user(struct pt_regs *regs) rt_noreturn;

void *arch_signal_ucontext_restore(rt_base_t user_sp, rt_base_t kernel_sp)
{
    struct signal_ucontext context;
    struct pt_regs *regs = (void *)kernel_sp;

    if (lwp_get_from_user(&context, (void *)user_sp, sizeof(context)) != sizeof(context) ||
        context.frame.r_era >= USER_VADDR_TOP || context.frame.r_sp >= USER_VADDR_TOP)
        sys_exit_group(EXIT_FAILURE);
    lwp_thread_signal_mask(rt_thread_self(), LWP_SIG_MASK_CMD_SET_MASK,
                           &context.save_sigmask, RT_NULL);
    *regs = context.frame;
    /* Never accept a privileged return mode from the user signal frame. */
    regs->r_prmd = 7;
    regs->r_euen = 1;
    rt_thread_self()->thread_idr = (void *)regs->r_tp;
    return regs;
}

void arch_thread_signal_enter(int signo, siginfo_t *info, void *exp_frame,
                              void *entry, lwp_sigset_t *mask)
{
    struct pt_regs *regs = exp_frame;
    struct signal_ucontext context;
    rt_ubase_t usp = RT_ALIGN_DOWN(regs->r_sp - sizeof(context), 16);
    extern void lwp_sigreturn(void);

    rt_memset(&context, 0, sizeof(context));
    rt_memcpy(&context.sigreturn, lwp_sigreturn, 8);
    context.save_sigmask = *mask;
    if (info)
        context.si = *info;
    context.frame = *regs;
    if (lwp_put_to_user((void *)usp, &context, sizeof(context)) != sizeof(context))
        sys_exit_group(EXIT_FAILURE);
    __asm__ volatile("dbar 0; ibar 0" ::: "memory");
    regs->r_sp = usp;
    regs->r_ra = usp;
    regs->r_era = entry ? (rt_ubase_t)entry : usp;
    regs->r_a0 = signo;
    regs->r_a1 = usp + offsetof(struct signal_ucontext, si);
    regs->r_a2 = usp + offsetof(struct signal_ucontext, frame);
    regs->r_prmd = 7;
    loongarch_restore_user(regs);
}

void loongarch_set_kernel_stack(void)
{
    rt_thread_t thread = rt_thread_self();
    rt_ubase_t top = RT_ALIGN_DOWN((rt_ubase_t)thread->stack_addr + thread->stack_size, 16);
    csr_write64(top, EXCEPTION_KS2);
}

void loongarch_ret_to_user(struct pt_regs *regs)
{
    rt_thread_self()->user_ctx.ctx = regs;
    if (lwp_check_exit_request())
        sys_exit(0);
    lwp_thread_signal_catch(regs);
}

void arch_syscall_set_errno(void *frame, int expected, int code)
{
    struct pt_regs *regs = frame;
    if ((rt_base_t)regs->r_a0 == expected)
        regs->r_a0 = code;
}

void lwp_exec_user(void *args, void *kernel_stack, void *entry)
{
    arch_start_umode(args, entry, (void *)USER_STACK_VEND, kernel_stack);
}

long _sys_fork(void);
long _sys_clone(void *args[]);
long sys_fork(void) { return _sys_fork(); }
long sys_vfork(void) { return _sys_fork(); }
long sys_clone(void *args[]) { return _sys_clone(args); }

int arch_backtrace_uthread(rt_thread_t thread)
{
    struct rt_hw_backtrace_frame frame;
    struct pt_regs *stack;

    if (thread && thread->lwp)
    {
        stack = thread->user_ctx.ctx;
        if ((long)stack > (unsigned long)thread->stack_addr
            && (long)stack < (unsigned long)thread->stack_addr + thread->stack_size)
        {
            frame.pc = stack->r_era;
            frame.fp = stack->r_fp;
            lwp_backtrace_frame(thread, &frame);
            return 0;
        }
        else
            return -1;
    }
    return -1;
}
