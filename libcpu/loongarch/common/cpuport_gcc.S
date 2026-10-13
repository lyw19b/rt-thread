/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-12     LoongsonLab  remove stack adjust.
 */

#include "stackframe.h"
#include "asm-generic.h"
#include "asm-offsets.h"

START_POINT(_rt_thread_entry)
    LONG_LD    ra, sp, PT_R14		/* thread exit */
    LONG_LD    a0, sp, PT_R13       /* parameter */
    LONG_LD    s0, sp, PT_R12       /* tentry */
    move       s1, ra
    /* jump to thread entry */
    jirl       ra, s0, 0
    /* run thread exit routine */
    jirl       zero, s1, 0

1:  /* never here */
	b          1b
START_POINT_END(_rt_thread_entry)
