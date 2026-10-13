/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include "stackframe.h"
#include "asm-generic.h"

.global arch_start_umode
arch_start_umode:
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
    b arch_start_umode

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
