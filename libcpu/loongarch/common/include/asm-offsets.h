/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef _ASM_ASM_OFFSETS_H
#define _ASM_ASM_OFFSETS_H


#ifdef __loongarch64
	#define LONGSIZE	8
    #define LONGMASK	7
    #define LONGLOG		3
#endif

/* LoongArch pt_regs offsets. */
#define PT_R0      (0  * LONGSIZE)
#define PT_R1      (1  * LONGSIZE)
#define PT_R2      (2  * LONGSIZE)
#define PT_R3      (3  * LONGSIZE)
#define PT_R4      (4  * LONGSIZE)
#define PT_R5      (5  * LONGSIZE)
#define PT_R6      (6  * LONGSIZE)
#define PT_R7      (7  * LONGSIZE)
#define PT_R8      (8  * LONGSIZE)
#define PT_R9      (9  * LONGSIZE)
#define PT_R10     (10 * LONGSIZE)
#define PT_R11     (11 * LONGSIZE)
#define PT_R12     (12 * LONGSIZE)  // r12 -> t0
#define PT_R13     (13 * LONGSIZE)  // r13 -> t1
#define PT_R14     (14 * LONGSIZE)  // r14 -> t2
#define PT_R15     (15 * LONGSIZE)
#define PT_R16     (16 * LONGSIZE)
#define PT_R17     (17 * LONGSIZE)
#define PT_R18     (18 * LONGSIZE)
#define PT_R19     (19 * LONGSIZE)
#define PT_R20     (20 * LONGSIZE)
#define PT_R21     (21 * LONGSIZE)
#define PT_R22     (22 * LONGSIZE)
#define PT_R23     (23 * LONGSIZE)
#define PT_R24     (24 * LONGSIZE)
#define PT_R25     (25 * LONGSIZE)
#define PT_R26     (26 * LONGSIZE)
#define PT_R27     (27 * LONGSIZE)
#define PT_R28     (28 * LONGSIZE)
#define PT_R29     (29 * LONGSIZE)
#define PT_R30     (30 * LONGSIZE)
#define PT_R31     (31 * LONGSIZE)
#define PT_CRMD    (32 * LONGSIZE)
#define PT_PRMD    (33 * LONGSIZE)
#define PT_EUEN    (34 * LONGSIZE)
#define PT_ESTAT   (35 * LONGSIZE)
#define PT_ERA     (36 * LONGSIZE)
#define PT_BVADDR  (37 * LONGSIZE)
#define PT_ECFG    (38 * LONGSIZE)
#define PT_RESERVED (39 * LONGSIZE)

#define PT_REG_END	(PT_ECFG + LONGSIZE)

#define PT_FPR0     (40 * LONGSIZE)
#define PT_FCSR     (72 * LONGSIZE)
#define PT_FCC      (73 * LONGSIZE)
#define PT_SIZE     (74 * LONGSIZE)

// use for context switch
#define RT_THREAD_RA    (0 * LONGSIZE)
#define RT_THREAD_S0    (1 * LONGSIZE)
#define RT_THREAD_S1    (2 * LONGSIZE)
#define RT_THREAD_S2    (3 * LONGSIZE)
#define RT_THREAD_S3    (4 * LONGSIZE)
#define RT_THREAD_S4    (5 * LONGSIZE)
#define RT_THREAD_S5    (6 * LONGSIZE)
#define RT_THREAD_S6    (7 * LONGSIZE)
#define RT_THREAD_S7    (8 * LONGSIZE)
#define RT_THREAD_S8    (9 * LONGSIZE)
#define RT_THREAD_FP    (10 * LONGSIZE)
#define RT_THREAD_TP    (11 * LONGSIZE)
#define RT_THREAD_SP    (12 * LONGSIZE)
#define RT_THREAD_CRMD  (13 * LONGSIZE)
#define RT_THREAD_PRMD  (14 * LONGSIZE)
#define RT_THREAD_FCSR  (15 * LONGSIZE)
#define RT_THREAD_FCC   (16 * LONGSIZE)

#define RT_THREAD_FPR24 (18 * LONGSIZE)
#define RT_THREAD_END   (26 * LONGSIZE)

#define PT_SWITCH_FRAME_SIZE RT_THREAD_END

#endif /* _ASM_ASM_OFFSETS_H */
