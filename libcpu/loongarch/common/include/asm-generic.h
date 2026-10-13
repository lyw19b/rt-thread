/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef __ASM_GENERIC_H__
#define __ASM_GENERIC_H__


#ifdef __loongarch64
    #define LONG_ADD    add.d
    #define LONG_ADDI   addi.d
    #define LONG_STUB   sub.d
    #define LONG_LD     ld.d
    #define LONG_ST     st.d
    #define LONG_FLD    fld.d
    #define LONG_FST    fst.d
#else
    #error "Now, RT-Thread only suport LoongArch64"
#endif


/* use to mark a start point where every task start from */
#define START_POINT(funcname)               \
    .global funcname;                       \
    .type funcname, %function;	            \
    funcname:

#define START_POINT_END(name)   \
    .size name, .-name;

#endif /* __ASM_GENERIC_H__ */
