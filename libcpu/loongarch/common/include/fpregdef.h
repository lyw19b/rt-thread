/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef _ASM_FPREGDEF_H
#define _ASM_FPREGDEF_H

#define fa0	$f0	/* argument registers, fa0/fa1 reused as fv0/fv1 for return value */
#define fa1	$f1
#define fa2	$f2
#define fa3	$f3
#define fa4	$f4
#define fa5	$f5
#define fa6	$f6
#define fa7	$f7
#define ft0	$f8	/* caller saved */
#define ft1	$f9
#define ft2	$f10
#define ft3	$f11
#define ft4	$f12
#define ft5	$f13
#define ft6	$f14
#define ft7	$f15
#define ft8	$f16
#define ft9	$f17
#define ft10	$f18
#define ft11	$f19
#define ft12	$f20
#define ft13	$f21
#define ft14	$f22
#define ft15	$f23
#define fs0	$f24	/* callee saved */
#define fs1	$f25
#define fs2	$f26
#define fs3	$f27
#define fs4	$f28
#define fs5	$f29
#define fs6	$f30
#define fs7	$f31

#define fcsr0	$fcsr0
#define fcsr1	$fcsr1
#define fcsr2	$fcsr2
#define fcsr3	$fcsr3

#endif /* _ASM_FPREGDEF_H */
