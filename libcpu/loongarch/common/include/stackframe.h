/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-12     LoongsonLab  add fpu support
 * 2025-03-13     LoongsonLab  fix save and restore
 */

#ifndef __ASM_STACKFRAME_H__
#define __ASM_STACKFRAME_H__

#include <rtconfig.h>
#include "asm-generic.h"
#include "regdef.h"
#include "asm-offsets.h"
#include "loongarch.h"

#ifdef __ASSEMBLY__

/* Describe each register bank once for both directions of the RT-Thread
 * frame ABI.
 * r0/sp/t0/t1/r21 require special handling and are excluded from this bank.
 */
.macro exception_gprs operation
    .irp reg,1,2,4,5,6,7,8,9,10,11,14,15,16,17,18,19,20,22,23,24,25,26,27,28,29,30,31
        \operation $r\reg, sp, (\reg * LONGSIZE)
    .endr
.endm

.macro exception_csr_save csr, offset
    csrrd   t0, \csr
    LONG_ST t0, sp, \offset
.endm

/* Vector stubs have already preserved t0/t1 in KS0/KS1. Local IRQs stay
 * masked until the complete frame and the kernel percpu register are ready.
 */
.macro SAVE_ALL
    move    t0, sp
    csrrd   t1, LOONGARCH_CSR_PRMD
    andi    t1, t1, CSR_PRMD_PPLV
    beqz    t1, .Lexception_stack_ready\@
    csrrd   sp, EXCEPTION_KS2
.Lexception_stack_ready\@:
    addi.d  sp, sp, -PT_SIZE
    LONG_ST t0, sp, PT_R3
    LONG_ST zero, sp, PT_R0
    LONG_ST zero, sp, PT_RESERVED
    LONG_ST zero, sp, PT_BVADDR

    csrrd   t0, EXCEPTION_KS0
    csrrd   t1, EXCEPTION_KS1
    LONG_ST t0, sp, PT_R12
    LONG_ST t1, sp, PT_R13
    exception_gprs LONG_ST

    exception_csr_save LOONGARCH_CSR_PRMD, PT_PRMD
    exception_csr_save LOONGARCH_CSR_CRMD, PT_CRMD
    exception_csr_save LOONGARCH_CSR_EUEN, PT_EUEN
    exception_csr_save LOONGARCH_CSR_ECFG, PT_ECFG
    exception_csr_save LOONGARCH_CSR_ESTAT, PT_ESTAT
    exception_csr_save LOONGARCH_CSR_ERA, PT_ERA

    LONG_LD t0, sp, PT_PRMD
    andi    t0, t0, CSR_PRMD_PPLV
    beqz    t0, .Lexception_kernel_regs\@
    LONG_ST u0, sp, PT_R21
    csrrd   u0, PERCPU_BASE_KS
.Lexception_kernel_regs\@:
    save_exception_fpu
.endm

/* Restore only the return CSRs. CRMD, EUEN, ECFG and ESTAT are diagnostic
 * snapshots; writing them here would change the existing exception semantics.
 */
.macro RESTORE_ALL_AND_RET
    restore_exception_fpu
    LONG_LD t0, sp, PT_ERA
    csrwr   t0, LOONGARCH_CSR_ERA
    LONG_LD t0, sp, PT_PRMD
    csrwr   t0, LOONGARCH_CSR_PRMD
    LONG_LD t0, sp, PT_PRMD
    andi    t0, t0, CSR_PRMD_PPLV
    beqz    t0, .Lexception_restore_kernel\@
    LONG_LD u0, sp, PT_R21
    addi.d  t0, sp, PT_SIZE
    csrwr   t0, EXCEPTION_KS2
.Lexception_restore_kernel\@:
    exception_gprs LONG_LD
    LONG_LD t0, sp, PT_R12
    LONG_LD t1, sp, PT_R13
    LONG_LD sp, sp, PT_R3
    ertn
.endm

/* Switch frames contain the ABI callee-saved integer bank plus tp. */
.macro switch_gprs operation, base
    .irp reg,23,24,25,26,27,28,29,30,31
        \operation $r\reg, \base, (RT_THREAD_S0 + (\reg - 23) * LONGSIZE)
    .endr
    \operation fp, \base, RT_THREAD_FP
    \operation tp, \base, RT_THREAD_TP
.endm

.macro cpu_save_nonscratch thread
    switch_gprs LONG_ST, \thread
    LONG_ST sp, \thread, RT_THREAD_SP
.endm

.macro cpu_restore_nonscratch thread
    LONG_LD ra, \thread, RT_THREAD_RA
    switch_gprs LONG_LD, \thread
.endm

.macro cpu_save_csr_reg thread
    csrrd   t1, LOONGARCH_CSR_PRMD
    stptr.d t1, \thread, RT_THREAD_PRMD
    csrrd   t1, LOONGARCH_CSR_CRMD
    stptr.d t1, \thread, RT_THREAD_CRMD
.endm

.macro cpu_restore_csr_reg thread
    ldptr.d t1, \thread, RT_THREAD_PRMD
    csrwr   t1, LOONGARCH_CSR_PRMD
    ldptr.d t1, \thread, RT_THREAD_CRMD
    csrwr   t1, LOONGARCH_CSR_CRMD
.endm

/* The assembler expands these banks; no runtime loops or temporary stack
 * storage are introduced. Exceptions save f0-f31; switches save f24-f31.
 */
.macro frame_fprs operation, offset, first
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        .if \reg >= \first
            \operation $f\reg, sp, (\offset + (\reg - \first) * LONGSIZE)
        .endif
    .endr
.endm

.macro frame_fpu_control_save fcsr_offset, fcc_offset
    movfcsr2gr t0, $fcsr0
    st.d t0, sp, \fcsr_offset
    .irp cc,0,1,2,3,4,5,6,7
        movcf2gr t0, $fcc\cc
        st.b t0, sp, (\fcc_offset + \cc)
    .endr
.endm

.macro frame_fpu_control_restore fcsr_offset, fcc_offset
    ld.d t0, sp, \fcsr_offset
    movgr2fcsr $fcsr0, t0
    .irp cc,0,1,2,3,4,5,6,7
        ld.bu t0, sp, (\fcc_offset + \cc)
        movgr2cf $fcc\cc, t0
    .endr
.endm

.macro save_exception_fpu
    frame_fprs fst.d, PT_FPR0, 0
    frame_fpu_control_save PT_FCSR, PT_FCC
.endm

.macro restore_exception_fpu
    frame_fprs fld.d, PT_FPR0, 0
    frame_fpu_control_restore PT_FCSR, PT_FCC
.endm

.macro save_switch_fpu
    frame_fprs fst.d, RT_THREAD_FPR24, 24
    frame_fpu_control_save RT_THREAD_FCSR, RT_THREAD_FCC
.endm

.macro restore_switch_fpu
    frame_fprs fld.d, RT_THREAD_FPR24, 24
    frame_fpu_control_restore RT_THREAD_FCSR, RT_THREAD_FCC
.endm

#endif /* __ASSEMBLY__ */
#endif /* __ASM_STACKFRAME_H__ */
