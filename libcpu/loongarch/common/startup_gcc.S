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


.macro init_direct_windows
    li.d    t0, CSR_DMW0_INIT
    csrwr   t0, LOONGARCH_CSR_DMWIN0
    li.d    t0, CSR_DMW1_INIT
    csrwr   t0, LOONGARCH_CSR_DMWIN1
.endm

/* Both boot paths enter C at PLV0, with IRQs masked and only scalar FP enabled. */
.macro init_privileged_state
    li.w    t0, 0xb0
    csrwr   t0, LOONGARCH_CSR_CRMD
    li.w    t0, 0x04
    csrwr   t0, LOONGARCH_CSR_PRMD
    li.w    t0, 0x01
    csrwr   t0, LOONGARCH_CSR_EUEN
.endm

/**
 * @brief    Enter the kernel on the boot CPU using firmware arguments a0-a2.
 *
 * @note     Establishes the direct-map windows, clears BSS, preserves firmware
 *           arguments, and selects the boot stack before entering C initialization.
 *           Local IRQs remain masked until the kernel starts scheduling.
 */
    .global         _start
    .section ".start", "ax"
_start:

    init_direct_windows

	/* Continue through the cached DMW1 alias at 0x9000.... */
	pcaddi  t1, 0
	li.d    t0, CSR_DMW1_BASE
	bstrins.d  t0, t1, (DMW_PABITS - 1), 0
	jirl	zero, t0, 0x10

    init_privileged_state

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
/**
 * @brief    Enter a secondary CPU through the IOCSR mailbox entry address.
 *
 * @note     Initializes CPU-local direct-map state and selects the stack indexed
 *           by hardware CPU ID. Shared BSS and firmware arguments belong to CPU 0
 *           and must not be cleared again. Invalid CPU IDs remain parked.
 */
    .global loongarch_secondary_entry
loongarch_secondary_entry:
    init_direct_windows
    la.abs  t0, secondary_cached
    jr      t0

secondary_cached:
    init_privileged_state
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
