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

/* The copied vector stub preserves t0/t1 before entering any of these paths.
 * Interrupts, synchronous exceptions and page faults each build one frame;
 * their return policies are kept separate below.
 */
    .balign 32
START_POINT(handle_vint)
    SAVE_ALL
    move    a0, sp
    bl      do_rt_dispatch_trap
    b       ret_from_exception
START_POINT_END(handle_vint)

/**
 * @brief Save a synchronous exception and dispatch its saved ESTAT in C.
 * @note The synchronous fault vectors listed below share this entry. BADV is
 *       captured in C only for address/alignment faults; FCSR is in the frame.
 */
    .balign 32
START_POINT(loongarch_exception_entry)
    SAVE_ALL
    move    a0, sp
    bl      loongarch_dispatch_exception
    b       ret_from_exception
START_POINT_END(loongarch_exception_entry)

/* Keep the vector-table symbols without emitting duplicate entry bodies. */
.global handle_ade, handle_ale, handle_bce, handle_bp
.global handle_fpe, handle_fpu, handle_ri
.set handle_ade, loongarch_exception_entry
.set handle_ale, loongarch_exception_entry
.set handle_bce, loongarch_exception_entry
.set handle_bp, loongarch_exception_entry
.set handle_fpe, loongarch_exception_entry
.set handle_fpu, loongarch_exception_entry
.set handle_ri, loongarch_exception_entry

/**
 * @brief Save a page fault and retry the instruction after a successful repair.
 * @note Page faults bypass deferred IRQ scheduling and user-return hooks, as
 *       before. The saved ESTAT distinguishes load, store and permission faults.
 */
    .balign 32
START_POINT(handle_tlb_load_ptw)
    SAVE_ALL
    csrrd   a2, LOONGARCH_CSR_BADV
    ST      a2, sp, PT_BVADDR
    move    a0, sp
    bl      do_page_fault
    b       .Lrestore_exception
START_POINT_END(handle_tlb_load_ptw)

.global handle_tlb_store_ptw, handle_tlb_modify_ptw
.set handle_tlb_store_ptw, handle_tlb_load_ptw
.set handle_tlb_modify_ptw, handle_tlb_load_ptw

/**
 * @brief Complete deferred scheduling for a saved interrupt/exception frame.
 * @note sp addresses the frame and local IRQs are masked. A Smart user return
 *       processes pending exit requests and signals before restoring registers.
 */
START_POINT(ret_from_exception)
#ifdef RT_USING_SMP
    move    a0, sp
    bl      rt_scheduler_do_irq_switch
#else
    la.abs  t0, rt_thread_switch_interrupt_flag
    LD      t1, t0, 0
    beqz    t1, .Lcheck_user_return
    ST      zero, t0, 0

    la.abs  t0, rt_interrupt_from_thread
    LD      a0, t0, 0
    la.abs  t0, rt_interrupt_to_thread
    LD      a1, t0, 0
    li.w    t0, CSR_CRMD_IE
    csrxchg zero, t0, LOONGARCH_CSR_CRMD
    bl      rt_hw_context_switch
#endif

.Lcheck_user_return:
#ifdef RT_USING_SMART
    LD      t0, sp, PT_PRMD
    andi    t0, t0, CSR_PRMD_PPLV
    beqz    t0, .Lrestore_exception
    b       arch_ret_to_user
#endif

.Lrestore_exception:
    RESTORE_ALL_AND_RET
START_POINT_END(ret_from_exception)

/**
 * @brief Refill a TLB entry without using the interrupted thread's stack.
 * @note TLBRSAVE preserves t0; the walker loads the even/odd leaf pair using
 *       the configured directory levels. Invalid leaves take the page-fault path.
 */
START_POINT(handle_tlb_sw_refill)
    csrwr   t0, LOONGARCH_CSR_TLBRSAVE
    csrrd   t0, LOONGARCH_CSR_PGD
    lddir   t0, t0, 3
    lddir   t0, t0, 1
    ldpte   t0, 0
    ldpte   t0, 1
    tlbfill
    csrrd   t0, LOONGARCH_CSR_TLBRSAVE
    ertn
START_POINT_END(handle_tlb_sw_refill)

/**
 * @brief Mask local IRQs and return the previous CRMD in a0.
 * @note The SMP symbol is rt_hw_local_irq_disable; no global lock is acquired.
 */
START_POINT(rt_hw_interrupt_disable)
    move    a0, zero
    b       rt_hw_interrupt_enable
START_POINT_END(rt_hw_interrupt_disable)

/**
 * @brief Restore CRMD.IE from a0 and preserve the other CRMD fields.
 * @param level Saved CRMD in a0, from the matching disable operation.
 * @note The SMP symbol is rt_hw_local_irq_enable; no global lock is released.
 */
START_POINT(rt_hw_interrupt_enable)
    li.w    t0, CSR_CRMD_IE
    csrxchg a0, t0, LOONGARCH_CSR_CRMD
    jr      ra
START_POINT_END(rt_hw_interrupt_enable)

/* Reserved fallback for callers without a populated vector slot. */
START_POINT(trap_entry)
    b       trap_entry
START_POINT_END(trap_entry)

/* Copied into each 512-byte vector slot. The handler pointer stays at byte 32
 * and the complete stub stays 40 bytes, matching set_handler(). */
    .balign 8
START_POINT(exception_vector_stub)
    csrwr   t0, EXCEPTION_KS0
    csrwr   t1, EXCEPTION_KS1
    pcaddi  t0, 0
    LD      t0, t0, 24
    jr      t0
    .space 12
    .dword 0
START_POINT_END(exception_vector_stub)
