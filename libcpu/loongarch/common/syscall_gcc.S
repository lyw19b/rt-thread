/* SPDX-License-Identifier: Apache-2.0 */
#include "stackframe.h"
#include "asm-generic.h"

/**
 * @brief    Save the syscall exception frame and dispatch the requested service.
 *
 * @note     The C dispatcher updates the saved result and return PC. Smart
 *           user returns pass through exit/signal handling; kernel calls restore
 *           their interrupted context directly.
 */
START_POINT(handle_syscall)
    SAVE_ALL
    move a0, sp
    la.abs t0, rt_dispatch_syscall
    jirl ra, t0, 0
#ifdef RT_USING_SMART
    LONG_LD t0, sp, PT_PRMD
    andi t0, t0, 3
    beqz t0, 1f
    la.abs t0, arch_ret_to_user
    jr t0
#endif
1:
    RESTORE_ALL_AND_RET
START_POINT_END(handle_syscall)
