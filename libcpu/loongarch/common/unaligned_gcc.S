/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */


#include "asm-generic.h"
#include "regdef.h"

/* These byte loops have no exception-table fixups. Callers provide accessible
 * memory and a width of 1..8; zero width returns -1 without accessing memory.
 */

/* a0 = source, a1 = result, a2 = width, a3 = sign extension.
 * Read high to low, preserving the original memory access order. The first
 * byte seeds the sign; each remaining byte extends the accumulated value.
 */
START_POINT(unaligned_read)
    beqz    a2, .Lread_empty
    add.d   t0, a0, a2
    addi.d  t0, t0, -1
    beqz    a3, .Lread_unsigned
    ld.b    t1, t0, 0
    b       .Lread_next
.Lread_unsigned:
    ld.bu   t1, t0, 0
.Lread_next:
    addi.d  a2, a2, -1
    beqz    a2, .Lread_done
    addi.d  t0, t0, -1
    ld.bu   t2, t0, 0
    slli.d  t1, t1, 8
    or      t1, t1, t2
    b       .Lread_next
.Lread_done:
    st.d    t1, a1, 0
    move    a0, zero
    jr      ra
.Lread_empty:
    li.w    a0, -1
    jr      ra
START_POINT_END(unaligned_read)

/* a0 = destination, a1 = value, a2 = width. Publish low bytes first. */
START_POINT(unaligned_write)
    beqz    a2, .Lwrite_empty
.Lwrite_byte:
    st.b    a1, a0, 0
    srli.d  a1, a1, 8
    addi.d  a0, a0, 1
    addi.d  a2, a2, -1
    bnez    a2, .Lwrite_byte
    move    a0, zero
    jr      ra
.Lwrite_empty:
    li.w    a0, -1
    jr      ra
START_POINT_END(unaligned_write)
