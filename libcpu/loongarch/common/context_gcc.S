/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-12     LoongsonLab  add debug info
 */

#include "rtconfig.h"
#include "asm-offsets.h"
#include "asm-generic.h"
#include "stackframe.h"


.section ".text", "ax"

/**
 * @brief    Save the current thread context and restore the selected thread.
 *
 * @note     Called with local IRQs masked. In SMP builds the scheduler context
 *           lock remains held until the new stack is active. The switch frame saves
 *           callee-saved registers and FPU state; an IRQ frame remains on the old stack.
 *
 * @param    from is the outgoing saved stack-pointer slot address in a0.
 *
 * @param    to is the incoming saved stack-pointer slot address in a1.
 *
 * @param    to_thread is the incoming thread in a2 for SMP builds.
 */
.globl rt_hw_context_switch
rt_hw_context_switch:
    /* Allocate the switch frame below the current kernel stack pointer. */
    addi.d   sp, sp, -RT_THREAD_END
    save_switch_fpu
    cpu_save_csr_reg sp
    cpu_save_nonscratch sp
    LONG_ST  ra, sp, RT_THREAD_RA
    LONG_ST  sp, a0, 0

    LONG_LD  sp, a1, 0
#ifdef RT_USING_SMP
    move a0, a2
    bl rt_cpus_lock_status_restore
#else
#ifdef RT_USING_SMART
    bl rt_thread_self
    bl lwp_aspace_switch
#endif
#endif
#ifdef RT_USING_SMART
    bl loongarch_set_kernel_stack
#endif
    # csrrd t0, LOONGARCH_CSR_CRMD
    restore_switch_fpu
    cpu_restore_nonscratch sp
    cpu_restore_csr_reg sp
    addi.d   sp, sp, RT_THREAD_END
    # csrrd t0, LOONGARCH_CSR_CRMD
    jr   ra


/**
 * @brief    Restore the first thread context on the local CPU.
 *
 * @note     Called by scheduler startup with local IRQs masked. No outgoing
 *           context is saved, and execution does not return to the startup caller.
 *
 * @param    to is the incoming saved stack-pointer slot address in a0.
 *
 * @param    to_thread is the incoming thread in a1 for SMP builds.
 */
.global rt_hw_context_switch_to
rt_hw_context_switch_to:
    LONG_LD  sp, a0, 0
#ifdef RT_USING_SMP
    move a0, a1
    bl rt_cpus_lock_status_restore
#else
#ifdef RT_USING_SMART
    bl rt_thread_self
    bl lwp_aspace_switch
#endif
#endif
#ifdef RT_USING_SMART
    bl loongarch_set_kernel_stack
#endif
    # csrrd t0, LOONGARCH_CSR_CRMD
    restore_switch_fpu
    cpu_restore_nonscratch sp
    cpu_restore_csr_reg sp
    addi.d   sp, sp, RT_THREAD_END
    # csrrd t0, LOONGARCH_CSR_CRMD
    jr   ra


/**
 * @brief    Wait for an interrupt before returning to the idle loop.
 */
.global cpu_idle
cpu_idle:
	idle   0x0
	ret
