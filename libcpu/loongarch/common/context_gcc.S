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

/*
 * void rt_hw_context_switch(rt_uint32 from, rt_uint32 to)
 * a0 --> from
 * a1 --> to
 */
.globl rt_hw_context_switch
rt_hw_context_switch:
    // adjust sp point to bottom of pt_regs
    addi.d   sp, sp, -RT_THREAD_END
    save_switch_fpu
    cpu_save_csr_reg sp
    cpu_save_nonscratch sp
    LONG_ST  ra, sp, RT_THREAD_RA
    LONG_ST  sp, a0, 0

    LONG_LD  sp, a1, 0
    move     t0, sp

    bl  rt_thread_self

#ifdef RT_USING_SMART
    bl  lwp_aspace_switch
    bl  loongarch_set_kernel_stack
#endif
    # csrrd t0, LOONGARCH_CSR_CRMD
    restore_switch_fpu
    cpu_restore_nonscratch sp
    cpu_restore_csr_reg sp
    addi.d   sp, sp, RT_THREAD_END
    # csrrd t0, LOONGARCH_CSR_CRMD
    jr   ra


/*
 * void rt_hw_context_switch_to(rt_ubase_t to);
 *
 * a0 --> to SP pointer
 */
.global rt_hw_context_switch_to
rt_hw_context_switch_to:
    LONG_LD  sp, a0, 0
    move     t0, sp

    bl  rt_thread_self

#ifdef RT_USING_SMART
    bl  lwp_aspace_switch
    bl  loongarch_set_kernel_stack
#endif
    # csrrd t0, LOONGARCH_CSR_CRMD
    restore_switch_fpu
    cpu_restore_nonscratch sp
    cpu_restore_csr_reg sp
    addi.d   sp, sp, RT_THREAD_END
    # csrrd t0, LOONGARCH_CSR_CRMD
    jr   ra


.global cpu_idle
cpu_idle:
	idle   0x0
	ret
