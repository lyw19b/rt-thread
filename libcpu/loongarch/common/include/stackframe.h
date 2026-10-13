/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-12     LoongsonLab  add fpu support
 * 2025-03-13     LoongsonLab  fix save and restore
 */

#ifndef __ASM_STACKFRAME_H__
#define __ASM_STACKFRAME_H__

#include "asm-generic.h"
#include "regdef.h"
#include "asm-offsets.h"
#include "loongarch.h"

#ifdef RT_USING_FPU
#include "fpregdef.h"
#endif


#ifdef __ASSEMBLY__
.macro SAVE_FPU

.endm

.macro BACKUP_T0T1
	csrwr	t0, EXCEPTION_KS0
	csrwr	t1, EXCEPTION_KS1
.endm

.macro RELOAD_T0T1
	csrrd   t0, EXCEPTION_KS0
	csrrd   t1, EXCEPTION_KS1
.endm

.macro	SAVE_TEMP
	RELOAD_T0T1
	LONG_ST	t0, sp, PT_R12
	LONG_ST	t1, sp, PT_R13
	LONG_ST	t2, sp, PT_R14
	LONG_ST	t3, sp, PT_R15
	LONG_ST	t4, sp, PT_R16
	LONG_ST	t5, sp, PT_R17
	LONG_ST	t6, sp, PT_R18
	LONG_ST	t7, sp, PT_R19
	LONG_ST	t8, sp, PT_R20
.endm

.macro	SAVE_STATIC
	LONG_ST	s0, sp, PT_R23
	LONG_ST	s1, sp, PT_R24
	LONG_ST	s2, sp, PT_R25
	LONG_ST	s3, sp, PT_R26
	LONG_ST	s4, sp, PT_R27
	LONG_ST	s5, sp, PT_R28
	LONG_ST	s6, sp, PT_R29
	LONG_ST	s7, sp, PT_R30
	LONG_ST	s8, sp, PT_R31
.endm


.macro	SAVE_SOME
	csrrd	t1, LOONGARCH_CSR_PRMD
	andi	t1, t1, 0x3	/* extract pplv bit */
	move	t0, sp
	beqz	t1, 8f
	/* Called from user mode, new stack. */
	csrrd   sp, EXCEPTION_KS2
8:
	addi.d  sp, sp, -PT_SIZE
	LONG_ST	t0, sp, PT_R3
	LONG_ST	zero, sp, PT_R0
	csrrd	t0, LOONGARCH_CSR_PRMD
	LONG_ST	t0, sp, PT_PRMD
	csrrd	t0, LOONGARCH_CSR_CRMD
	LONG_ST	t0, sp, PT_CRMD
	csrrd	t0, LOONGARCH_CSR_EUEN
	LONG_ST t0, sp, PT_EUEN
	csrrd	t0, LOONGARCH_CSR_ECFG
	LONG_ST	t0, sp, PT_ECFG
	csrrd	t0, LOONGARCH_CSR_ESTAT
	LONG_ST	t0, sp, PT_ESTAT
	LONG_ST	ra, sp, PT_R1
	LONG_ST	a0, sp, PT_R4
	LONG_ST	a1, sp, PT_R5
	LONG_ST	a2, sp, PT_R6
	LONG_ST	a3, sp, PT_R7
	LONG_ST	a4, sp, PT_R8
	LONG_ST	a5, sp, PT_R9
	LONG_ST	a6, sp, PT_R10
	LONG_ST	a7, sp, PT_R11
	csrrd	ra, LOONGARCH_CSR_ERA
	LONG_ST	ra, sp, PT_ERA
	LONG_ST	tp, sp, PT_R2
	LONG_ST	fp, sp, PT_R22

	/* Set thread_info if we're coming from user mode */
	csrrd	t0, LOONGARCH_CSR_PRMD
	andi	t0, t0, 0x3	/* extract pplv bit */
	beqz	t0, 9f

	LONG_ST	u0, sp, PT_R21
	csrrd	u0, PERCPU_BASE_KS
9:
.endm


.macro SAVE_ALL
	SAVE_SOME
	SAVE_TEMP
	SAVE_STATIC
    save_exception_fpu
.endm


.macro	RESTORE_TEMP
	LONG_LD	t0, sp, PT_R12
	LONG_LD	t1, sp, PT_R13
	LONG_LD	t2, sp, PT_R14
	LONG_LD	t3, sp, PT_R15
	LONG_LD	t4, sp, PT_R16
	LONG_LD	t5, sp, PT_R17
	LONG_LD	t6, sp, PT_R18
	LONG_LD	t7, sp, PT_R19
	LONG_LD	t8, sp, PT_R20
.endm

.macro	RESTORE_STATIC
	LONG_LD	s0, sp, PT_R23
	LONG_LD	s1, sp, PT_R24
	LONG_LD	s2, sp, PT_R25
	LONG_LD	s3, sp, PT_R26
	LONG_LD	s4, sp, PT_R27
	LONG_LD	s5, sp, PT_R28
	LONG_LD	s6, sp, PT_R29
	LONG_LD	s7, sp, PT_R30
	LONG_LD	s8, sp, PT_R31
.endm


.macro	RESTORE_SOME
	LONG_LD	a0, sp, PT_PRMD
	andi    a0, a0, 0x3	/* extract pplv bit */
	beqz    a0, 8f
	LONG_LD  u0, sp, PT_R21
8:
	LONG_LD	a0, sp, PT_ERA
	csrwr	a0, LOONGARCH_CSR_ERA
	LONG_LD	a0, sp, PT_PRMD
	csrwr	a0, LOONGARCH_CSR_PRMD
	LONG_LD	ra, sp, PT_R1
	LONG_LD	a0, sp, PT_R4
	LONG_LD	a1, sp, PT_R5
	LONG_LD	a2, sp, PT_R6
	LONG_LD	a3, sp, PT_R7
	LONG_LD	a4, sp, PT_R8
	LONG_LD	a5, sp, PT_R9
	LONG_LD	a6, sp, PT_R10
	LONG_LD	a7, sp, PT_R11
	LONG_LD	fp, sp, PT_R22

	LONG_LD tp, sp, PT_PRMD
    andi tp, tp, 3
    beqz tp, 9f
    addi.d tp, sp, PT_SIZE
    csrwr tp, EXCEPTION_KS2
9:

	LONG_LD	tp, sp, PT_R2
.endm

.macro	RESTORE_SP_AND_RET
	LONG_LD	sp, sp, PT_R3
	ertn
.endm

.macro RESTORE_ALL_AND_RET
    restore_exception_fpu
	RESTORE_STATIC
	RESTORE_TEMP
	RESTORE_SOME
	RESTORE_SP_AND_RET
.endm


.macro	cpu_save_nonscratch thread
	LONG_ST	s0, \thread, RT_THREAD_S0
	LONG_ST	s1, \thread, RT_THREAD_S1
	LONG_ST	s2, \thread, RT_THREAD_S2
	LONG_ST	s3, \thread, RT_THREAD_S3
	LONG_ST	s4, \thread, RT_THREAD_S4
	LONG_ST	s5, \thread, RT_THREAD_S5
	LONG_ST	s6, \thread, RT_THREAD_S6
	LONG_ST	s7, \thread, RT_THREAD_S7
	LONG_ST	s8, \thread, RT_THREAD_S8
	LONG_ST	sp, \thread, RT_THREAD_SP
	LONG_ST	fp, \thread, RT_THREAD_FP
	LONG_ST	tp, \thread, RT_THREAD_TP
.endm

.macro	cpu_save_csr_reg thread
	csrrd	t1, LOONGARCH_CSR_PRMD
	stptr.d	t1, \thread, RT_THREAD_PRMD
	csrrd	t1, LOONGARCH_CSR_CRMD
	stptr.d	t1, \thread, RT_THREAD_CRMD
.endm


.macro	cpu_restore_nonscratch thread
	LONG_LD	ra, \thread, RT_THREAD_RA
	LONG_LD	s0, \thread, RT_THREAD_S0
	LONG_LD	s1, \thread, RT_THREAD_S1
	LONG_LD	s2, \thread, RT_THREAD_S2
	LONG_LD	s3, \thread, RT_THREAD_S3
	LONG_LD	s4, \thread, RT_THREAD_S4
	LONG_LD	s5, \thread, RT_THREAD_S5
	LONG_LD	s6, \thread, RT_THREAD_S6
	LONG_LD	s7, \thread, RT_THREAD_S7
	LONG_LD	s8, \thread, RT_THREAD_S8
	LONG_LD	fp, \thread, RT_THREAD_FP
	LONG_LD	tp, \thread, RT_THREAD_TP
.endm

.macro	cpu_restore_csr_reg thread
	ldptr.d	t1, \thread, RT_THREAD_PRMD
	csrwr	t1, LOONGARCH_CSR_PRMD
	ldptr.d	t1, \thread, RT_THREAD_CRMD
	csrwr	t1, LOONGARCH_CSR_CRMD
.endm

.macro	cpu_restore_sp thread
	LONG_LD	sp, \thread, RT_THREAD_SP
.endm

/* Save all FPRs because an interrupt may preempt any instruction. */
.macro save_exception_fpu
    fst.d $f0, sp, 320
    fst.d $f1, sp, 328
    fst.d $f2, sp, 336
    fst.d $f3, sp, 344
    fst.d $f4, sp, 352
    fst.d $f5, sp, 360
    fst.d $f6, sp, 368
    fst.d $f7, sp, 376
    fst.d $f8, sp, 384
    fst.d $f9, sp, 392
    fst.d $f10, sp, 400
    fst.d $f11, sp, 408
    fst.d $f12, sp, 416
    fst.d $f13, sp, 424
    fst.d $f14, sp, 432
    fst.d $f15, sp, 440
    fst.d $f16, sp, 448
    fst.d $f17, sp, 456
    fst.d $f18, sp, 464
    fst.d $f19, sp, 472
    fst.d $f20, sp, 480
    fst.d $f21, sp, 488
    fst.d $f22, sp, 496
    fst.d $f23, sp, 504
    fst.d $f24, sp, 512
    fst.d $f25, sp, 520
    fst.d $f26, sp, 528
    fst.d $f27, sp, 536
    fst.d $f28, sp, 544
    fst.d $f29, sp, 552
    fst.d $f30, sp, 560
    fst.d $f31, sp, 568
    movfcsr2gr t0, $fcsr0
    st.d t0, sp, 576
    movcf2gr t0, $fcc0
    st.b t0, sp, 584
    movcf2gr t0, $fcc1
    st.b t0, sp, 585
    movcf2gr t0, $fcc2
    st.b t0, sp, 586
    movcf2gr t0, $fcc3
    st.b t0, sp, 587
    movcf2gr t0, $fcc4
    st.b t0, sp, 588
    movcf2gr t0, $fcc5
    st.b t0, sp, 589
    movcf2gr t0, $fcc6
    st.b t0, sp, 590
    movcf2gr t0, $fcc7
    st.b t0, sp, 591
.endm

.macro restore_exception_fpu
    fld.d $f0, sp, 320
    fld.d $f1, sp, 328
    fld.d $f2, sp, 336
    fld.d $f3, sp, 344
    fld.d $f4, sp, 352
    fld.d $f5, sp, 360
    fld.d $f6, sp, 368
    fld.d $f7, sp, 376
    fld.d $f8, sp, 384
    fld.d $f9, sp, 392
    fld.d $f10, sp, 400
    fld.d $f11, sp, 408
    fld.d $f12, sp, 416
    fld.d $f13, sp, 424
    fld.d $f14, sp, 432
    fld.d $f15, sp, 440
    fld.d $f16, sp, 448
    fld.d $f17, sp, 456
    fld.d $f18, sp, 464
    fld.d $f19, sp, 472
    fld.d $f20, sp, 480
    fld.d $f21, sp, 488
    fld.d $f22, sp, 496
    fld.d $f23, sp, 504
    fld.d $f24, sp, 512
    fld.d $f25, sp, 520
    fld.d $f26, sp, 528
    fld.d $f27, sp, 536
    fld.d $f28, sp, 544
    fld.d $f29, sp, 552
    fld.d $f30, sp, 560
    fld.d $f31, sp, 568
    ld.d t0, sp, 576
    movgr2fcsr $fcsr0, t0
    ld.bu t0, sp, 584
    movgr2cf $fcc0, t0
    ld.bu t0, sp, 585
    movgr2cf $fcc1, t0
    ld.bu t0, sp, 586
    movgr2cf $fcc2, t0
    ld.bu t0, sp, 587
    movgr2cf $fcc3, t0
    ld.bu t0, sp, 588
    movgr2cf $fcc4, t0
    ld.bu t0, sp, 589
    movgr2cf $fcc5, t0
    ld.bu t0, sp, 590
    movgr2cf $fcc6, t0
    ld.bu t0, sp, 591
    movgr2cf $fcc7, t0
.endm

.macro save_switch_fpu
    fst.d $f0, sp, 144
    fst.d $f1, sp, 152
    fst.d $f2, sp, 160
    fst.d $f3, sp, 168
    fst.d $f4, sp, 176
    fst.d $f5, sp, 184
    fst.d $f6, sp, 192
    fst.d $f7, sp, 200
    fst.d $f8, sp, 208
    fst.d $f9, sp, 216
    fst.d $f10, sp, 224
    fst.d $f11, sp, 232
    fst.d $f12, sp, 240
    fst.d $f13, sp, 248
    fst.d $f14, sp, 256
    fst.d $f15, sp, 264
    fst.d $f16, sp, 272
    fst.d $f17, sp, 280
    fst.d $f18, sp, 288
    fst.d $f19, sp, 296
    fst.d $f20, sp, 304
    fst.d $f21, sp, 312
    fst.d $f22, sp, 320
    fst.d $f23, sp, 328
    fst.d $f24, sp, 336
    fst.d $f25, sp, 344
    fst.d $f26, sp, 352
    fst.d $f27, sp, 360
    fst.d $f28, sp, 368
    fst.d $f29, sp, 376
    fst.d $f30, sp, 384
    fst.d $f31, sp, 392
    movfcsr2gr t0, $fcsr0
    st.d t0, sp, 120
    movcf2gr t0, $fcc0
    st.b t0, sp, 128
    movcf2gr t0, $fcc1
    st.b t0, sp, 129
    movcf2gr t0, $fcc2
    st.b t0, sp, 130
    movcf2gr t0, $fcc3
    st.b t0, sp, 131
    movcf2gr t0, $fcc4
    st.b t0, sp, 132
    movcf2gr t0, $fcc5
    st.b t0, sp, 133
    movcf2gr t0, $fcc6
    st.b t0, sp, 134
    movcf2gr t0, $fcc7
    st.b t0, sp, 135
.endm

.macro restore_switch_fpu
    fld.d $f0, sp, 144
    fld.d $f1, sp, 152
    fld.d $f2, sp, 160
    fld.d $f3, sp, 168
    fld.d $f4, sp, 176
    fld.d $f5, sp, 184
    fld.d $f6, sp, 192
    fld.d $f7, sp, 200
    fld.d $f8, sp, 208
    fld.d $f9, sp, 216
    fld.d $f10, sp, 224
    fld.d $f11, sp, 232
    fld.d $f12, sp, 240
    fld.d $f13, sp, 248
    fld.d $f14, sp, 256
    fld.d $f15, sp, 264
    fld.d $f16, sp, 272
    fld.d $f17, sp, 280
    fld.d $f18, sp, 288
    fld.d $f19, sp, 296
    fld.d $f20, sp, 304
    fld.d $f21, sp, 312
    fld.d $f22, sp, 320
    fld.d $f23, sp, 328
    fld.d $f24, sp, 336
    fld.d $f25, sp, 344
    fld.d $f26, sp, 352
    fld.d $f27, sp, 360
    fld.d $f28, sp, 368
    fld.d $f29, sp, 376
    fld.d $f30, sp, 384
    fld.d $f31, sp, 392
    ld.d t0, sp, 120
    movgr2fcsr $fcsr0, t0
    ld.bu t0, sp, 128
    movgr2cf $fcc0, t0
    ld.bu t0, sp, 129
    movgr2cf $fcc1, t0
    ld.bu t0, sp, 130
    movgr2cf $fcc2, t0
    ld.bu t0, sp, 131
    movgr2cf $fcc3, t0
    ld.bu t0, sp, 132
    movgr2cf $fcc4, t0
    ld.bu t0, sp, 133
    movgr2cf $fcc5, t0
    ld.bu t0, sp, 134
    movgr2cf $fcc6, t0
    ld.bu t0, sp, 135
    movgr2cf $fcc7, t0
.endm


#endif /* __ASSEMBLY__ */
#endif /* __ASM_STACKFRAME_H__ */
