/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include "stackframe.h"
#include "asm-generic.h"

.global arch_start_umode
arch_start_umode:
    move ra, zero
.Lenter_user:
    li.w t0, CSR_CRMD_IE
    csrxchg zero, t0, LOONGARCH_CSR_CRMD
    csrwr a3, EXCEPTION_KS2
    csrwr a1, LOONGARCH_CSR_ERA
    li.w t0, 7
    csrwr t0, LOONGARCH_CSR_PRMD
    move sp, a2
    ertn

.global arch_crt_start_umode
arch_crt_start_umode:
    move s0, a0
    move s1, a1
    move s2, a3
    move a0, a2
    bl loongarch_prepare_thread_return
    move ra, a0
    move a2, a0
    move a0, s0
    move a1, s1
    move a3, s2
    b .Lenter_user

.global arch_fork_exit
.global arch_clone_exit
.global arch_syscall_exit
.global arch_ret_to_user
arch_fork_exit:
arch_clone_exit:
arch_syscall_exit:
arch_ret_to_user:
    move a0, sp
    bl loongarch_ret_to_user
    RESTORE_ALL_AND_RET

.global loongarch_restore_user
loongarch_restore_user:
    move sp, a0
    RESTORE_ALL_AND_RET

.global arch_get_tidr
arch_get_tidr:
    move a0, tp
    ret

.global arch_set_thread_area
.global arch_set_tidr
arch_set_thread_area:
arch_set_tidr:
    move tp, a0
    ret

.global lwp_sigreturn
lwp_sigreturn:
    ori a7, zero, 0xfe
    syscall 0

/* Copied to a newly created user thread's stack for a normal C return. */
.global lwp_thread_return
lwp_thread_return:
    ori a7, zero, 1
    syscall 0
