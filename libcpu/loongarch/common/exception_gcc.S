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

/* One entry template for RT-Thread C handlers. The vector stub owns t0/t1;
 * SAVE_ALL completes the frame before handler arguments are prepared.
 */
.macro exception_dispatch name, handler, badv=0, fcsr=0
    .align 5
    START_POINT(\name)
    SAVE_ALL
    .if \badv
        csrrd t0, LOONGARCH_CSR_BADV
        LONG_ST t0, sp, PT_BVADDR
    .endif
#ifdef RT_USING_FPU
    .if \fcsr
        movfcsr2gr a1, $fcsr0
    .endif
#endif
    move    a0, sp
    la.abs  t0, \handler
    jirl    ra, t0, 0
    la.abs  t0, ret_from_exception
    jirl    zero, t0, 0
    START_POINT_END(\name)
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


exception_dispatch handle_vint, do_rt_dispatch_trap
exception_dispatch handle_ade, do_ade, 1
exception_dispatch handle_ale, do_ale, 1
exception_dispatch handle_bce, do_bce
exception_dispatch handle_bp, do_bp
exception_dispatch handle_fpe, do_fpe, 0, 1
exception_dispatch handle_fpu, do_fpu
exception_dispatch handle_ri, do_ri

    .align 2
    .global trap_entry
trap_entry:
    /* We don't use unifid exception enter entry*/
    b  trap_entry
    ret


/**
 * @brief Resolve a load, store or write-permission fault through one entry.
 * @note ESTAT in the saved frame distinguishes the operation. Keep the three
 *       exported vector targets, and return directly to retry the instruction.
 */
.global handle_tlb_load_ptw
.global handle_tlb_store_ptw
.global handle_tlb_modify_ptw
handle_tlb_load_ptw:
handle_tlb_store_ptw:
handle_tlb_modify_ptw:
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

