/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-14     LoongsonLab  refactor exception routine
 * 2025-03-22     LoongsonLab  fix tlb refill and page fault
 */

#include "loongarch.h"
#include "regdef.h"
#include "asm-generic.h"
#include "stackframe.h"

#ifdef RT_USING_SMP
#define rt_hw_interrupt_enable rt_hw_local_irq_enable
#define rt_hw_interrupt_disable rt_hw_local_irq_disable
#endif

.macro  build_prep_badv
    csrrd     t0, LOONGARCH_CSR_BADV
    LONG_ST   t0, sp, PT_BVADDR
.endm

.macro  build_prep_fcsr
#ifdef RT_USING_FPU
    movfcsr2gr  a1, $fcsr0
#endif
.endm

.macro  build_prep_none
.endm

.macro  BUILD_HANDLER exception handler prep
    .align  5
    START_POINT(handle_\exception)
_666_\exception:
    SAVE_ALL
    build_prep_\prep
    move    a0, sp
    la.abs  t0, do_\handler
    jirl    ra, t0, 0
    la.abs  t0, ret_from_exception
    jirl    ra, t0, 0
    START_POINT_END(handle_\exception)
.endm


/**
 * @brief    Complete deferred scheduling before restoring an exception frame.
 *
 * @note     The saved frame is at sp and local IRQs are masked. Smart user
 *           returns also process pending exit requests and signals before restoration.
 */
START_POINT(ret_from_exception)
ret_from_exception:
#ifdef RT_USING_SMP
    move a0, sp
    bl rt_scheduler_do_irq_switch
#else
    la.abs  t0, rt_thread_switch_interrupt_flag
    LONG_LD t1, t0, 0
    beqz    t1, exception_resume /*resume exec*/
    LONG_ST zero, t0, 0

exception_context_switch:/*context_switch*/
    la.abs  t0, rt_interrupt_from_thread
    LONG_LD a0, t0, 0
    la.abs  t0, rt_interrupt_to_thread
    LONG_LD a1, t0, 0
    /* Disable Interrupt*/
    li.w    t0, CSR_CRMD_IE
    csrxchg  zero, t0, LOONGARCH_CSR_CRMD
    la.abs  t0, rt_hw_context_switch
    jirl    ra, t0, 0

exception_resume:/*resume exec*/
#endif
#ifdef RT_USING_SMART
    LONG_LD t0, sp, PT_PRMD
    andi    t0, t0, 0x3 /* extract pplv bit */
    beqz    t0, exception_resume_kernel
exception_resume_user:
    la.abs  t0, arch_ret_to_user
    jirl    ra, t0, 0
#endif

exception_resume_kernel: /*resume_kernel*/
    RESTORE_ALL_AND_RET
never_return:
    b never_return
START_POINT_END(ret_from_exception)


/* handle interrupt */
BUILD_HANDLER vint rt_dispatch_trap none

BUILD_HANDLER ade ade badv
BUILD_HANDLER ale ale badv
BUILD_HANDLER bce bce none
BUILD_HANDLER bp bp none
BUILD_HANDLER fpe fpe fcsr
BUILD_HANDLER fpu fpu none
BUILD_HANDLER ri ri none

    .align 2
    .global trap_entry
trap_entry:
    /* We don't use unifid exception enter entry*/
    b  trap_entry
    ret


/**
 * @brief    Save the context and resolve a load or instruction-fetch fault.
 *
 * @note     Shared by software refill and hardware PTW. BADV is copied into the
 *           exception frame before calling C; a repaired fault retries the instruction.
 */
.global handle_tlb_load_ptw
handle_tlb_load_ptw:
    // save t0,t1
    SAVE_ALL
    csrrd       a2, LOONGARCH_CSR_BADV
    move        a0, sp
    LONG_ST     a2, sp, PT_BVADDR
    la.abs      t0, do_page_fault
    jirl        ra, t0, 0
    RESTORE_ALL_AND_RET

/**
 * @brief    Save the context and resolve a store fault.
 *
 * @note     Shared by software refill and hardware PTW. BADV is copied into the
 *           exception frame before calling C; a repaired fault retries the instruction.
 */
.global handle_tlb_store_ptw
handle_tlb_store_ptw:
    // save t0,t1
    SAVE_ALL
    csrrd       a2, LOONGARCH_CSR_BADV
    move        a0, sp
    LONG_ST     a2, sp, PT_BVADDR
    la.abs      t0, do_page_fault
    jirl        ra, t0, 0
    RESTORE_ALL_AND_RET

/**
 * @brief    Save the context and resolve a write-permission fault.
 *
 * @note     Shared by software refill and hardware PTW. BADV is copied into the
 *           exception frame before calling C; a repaired fault retries the instruction.
 */
.global handle_tlb_modify_ptw
handle_tlb_modify_ptw:
    // save t0,t1
    SAVE_ALL
    csrrd       a2, LOONGARCH_CSR_BADV
    move        a0, sp
    LONG_ST     a2, sp, PT_BVADDR
    la.abs      t0, do_page_fault
    jirl        ra, t0, 0
    RESTORE_ALL_AND_RET


/**
 * @brief    Refill a TLB entry by walking the page tables in software.
 *
 * @note     Runs in refill context without using a kernel stack. TLBRSAVE
 *           preserves t0 while PGD and the configured directory levels locate the
 *           even/odd leaf pair. Invalid leaves are handled by the page-fault path.
 */
START_POINT(handle_tlb_sw_refill)
    csrwr       t0, LOONGARCH_CSR_TLBRSAVE
    csrrd       t0, LOONGARCH_CSR_PGD
    lddir       t0, t0, 3
    lddir       t0, t0, 1
    ldpte       t0, 0
    ldpte       t0, 1
    tlbfill
    csrrd       t0, LOONGARCH_CSR_TLBRSAVE
    ertn
START_POINT_END(handle_tlb_sw_refill)


# Enable and Disable Interrupt
/**
 * @brief    Restore the local interrupt-enable bit from a saved CRMD value.
 *
 * @note     Only CRMD.IE is modified. In SMP builds this symbol is exported as
 *           rt_hw_local_irq_enable(); it does not release the global CPU lock.
 *
 * @param    level is the value in a0 returned by the matching disable operation.
 */
.global rt_hw_interrupt_enable
rt_hw_interrupt_enable:
    li.w     t0, CSR_CRMD_IE
    csrxchg  a0, t0, LOONGARCH_CSR_CRMD
    jirl     zero, ra, 0

/**
 * @brief    Mask local interrupts and return the previous CRMD value.
 *
 * @note     In SMP builds this symbol is rt_hw_local_irq_disable(). It does
 *           not acquire a scheduler or global CPU lock.
 *
 * @return   The saved CRMD value in a0, for the matching restore operation.
 */
.global rt_hw_interrupt_disable
rt_hw_interrupt_disable:
    move     a0, zero
    li.w     t0, CSR_CRMD_IE
    csrxchg  a0, t0, LOONGARCH_CSR_CRMD
    jirl	 zero, ra, 0


/* Each 512-byte vector slot contains only this position-independent stub. */
.balign 8
.global exception_vector_stub
exception_vector_stub:
    csrwr t0, EXCEPTION_KS0
    csrwr t1, EXCEPTION_KS1
    pcaddi t0, 0
    ld.d t0, t0, 24
    jr t0
    .space 12
    .dword 0

