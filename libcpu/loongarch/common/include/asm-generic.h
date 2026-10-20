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
#define ADD  add.d
#define ADDI addi.d
#define STUB sub.d
#define LD   ld.d
#define ST   st.d
#define FLD  fld.d
#define FST  fst.d
#else
#error "Now, RT-Thread only suport LoongArch64"
#endif


/* Export assembly function symbols and record their sizes. */
/* clang-format off */
#define START_POINT(funcname)  \
    .global funcname;          \
    .type funcname, %function; \
    funcname:

#define START_POINT_END(name) \
    .size name, .-name;
/* clang-format on */

#endif /* __ASM_GENERIC_H__ */
