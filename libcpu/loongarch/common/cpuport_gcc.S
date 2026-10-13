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
    /* The initial switch frame carries entry, argument and exit in s0-s2. */
    move       a0, s1
    jirl       ra, s0, 0
    jirl       zero, s2, 0
START_POINT_END(_rt_thread_entry)
