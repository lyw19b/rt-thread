/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#include "loongarch.h"
#include "regdef.h"
#include "rtconfig.h"
#include "asm-offsets.h"


    .global         _start
    .section ".start", "ax"
_start:

	# Set up Direct Map windows registers
	li.d	t0, CSR_DMW0_INIT	# WUC, PLV0, 0x8000 xxxx xxxx xxxx
	csrwr	t0, LOONGARCH_CSR_DMWIN0
	li.d	t0, CSR_DMW1_INIT	# CAC, PLV0, 0x9000 xxxx xxxx xxxx
	csrwr	t0, LOONGARCH_CSR_DMWIN1

	# Jump to 0x8000 high address, cacheable
	pcaddi  t1, 0
	li.d    t0, CSR_DMW1_BASE
	bstrins.d  t0, t1, (DMW_PABITS - 1), 0
	jirl	zero, t0, 0x10

	/* Enable PG */
	li.w	t0, 0xb0		# PLV=0, IE=0, PG=1
	csrwr	t0, LOONGARCH_CSR_CRMD
	li.w	t0, 0x04		# PLV=0, PIE=1, PWE=0
	csrwr	t0, LOONGARCH_CSR_PRMD
	li.w	t0, 0x01		# FPE=1, SXE=0, ASXE=0, BTE=0
	csrwr	t0, LOONGARCH_CSR_EUEN

	/* Clear BSS */
	bl 		init_bss

	/* Save */
	bl 		save_boot_args

	/* Clear Register */
	bl		init_clear_regs

	/* init stack */
	la.pcrel	sp, __stack_end__

	bl 		init_cpu_early

debug_trace:
	la.pcrel    t0, rtthread_startup
	jirl        zero, t0, 0

never_return:
	b  never_return


init_bss:
	la.pcrel	t0, __bss_start		# clear .bss
	st.d		zero, t0, 0
	la.pcrel	t1, __bss_end - LONGSIZE
1:
	addi.d		t0, t0, LONGSIZE
	st.d		zero, t0, 0
	bne		    t0, t1, 1b
debug_trace5:
	ret

save_boot_args:
	la.pcrel	t0, fw_arg0
	st.d		a0, t0, 0		# firmware arguments
	la.pcrel	t0, fw_arg1
	st.d		a1, t0, 0
	la.pcrel	t0, fw_arg2
	st.d		a2, t0, 0
	ret

init_clear_regs:
	/* Clear Register */
	move    tp, zero
	move    sp, zero
	move    a0, zero
	move    a1, zero
	move    a2, zero
	move    a3, zero
	move    a4, zero
	move    a5, zero
	move    a6, zero
	move    a7, zero
	move    t0, zero
	move    t1, zero
	move    t2, zero
	move    t3, zero
	move    t4, zero
	move    t5, zero
	move    t6, zero
	move    t7, zero
	move    t8, zero
	move    u0, zero
	move    fp, zero
	move    s0, zero
	move    s1, zero
	move    s2, zero
	move    s3, zero
	move    s4, zero
	move    s5, zero
	move    s6, zero
	move    s7, zero
	move    s8, zero
	ret

#ifdef RT_USING_SMP
    .section ".text", "ax"
    .global loongarch_secondary_entry
loongarch_secondary_entry:
    li.d    t0, CSR_DMW0_INIT
    csrwr   t0, LOONGARCH_CSR_DMWIN0
    li.d    t0, CSR_DMW1_INIT
    csrwr   t0, LOONGARCH_CSR_DMWIN1
    la.abs  t0, secondary_cached
    jr      t0

secondary_cached:
    li.w    t0, 0xb0
    csrwr   t0, LOONGARCH_CSR_CRMD
    li.w    t0, 0x04
    csrwr   t0, LOONGARCH_CSR_PRMD
    li.w    t0, 1
    csrwr   t0, LOONGARCH_CSR_EUEN
    move    tp, zero
    csrrd   t0, LOONGARCH_CSR_CPUID
    andi    t0, t0, 0x3ff

    li.w    t1, RT_CPUS_NR
    bgeu    t0, t1, secondary_park
    beqz    t0, secondary_park

#if RT_CPUS_NR > 1
    /* CPU n uses stack n-1, whose top is base + n * stack_size. */
    li.w    t1, __STACKSIZE__
    mul.d   t0, t0, t1
    la.abs  sp, loongarch_secondary_stacks
    add.d   sp, sp, t0
    bl      loongarch_secondary_start
#endif

secondary_park:
    idle    0
    b       secondary_park
#endif
