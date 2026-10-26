/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    execute scalar ALE regression instructions
 */
#ifndef LOONGARCH_ALE_CASES_H
#define LOONGARCH_ALE_CASES_H

#include <stdint.h>

static const struct ale_case
{
    unsigned char size, store, floating, sign;
} ale_cases[] = {
    { 2, 0, 0, 1 }, /* ld.h */
    { 2, 0, 0, 0 }, /* ld.hu */
    { 2, 1, 0, 0 }, /* st.h */
    { 4, 0, 0, 1 }, /* ld.w */
    { 4, 0, 0, 0 }, /* ld.wu */
    { 4, 1, 0, 0 }, /* st.w */
    { 8, 0, 0, 0 }, /* ld.d */
    { 8, 1, 0, 0 }, /* st.d */
    { 4, 0, 1, 0 }, /* fld.s */
    { 4, 1, 1, 0 }, /* fst.s */
    { 8, 0, 1, 0 }, /* fld.d */
    { 8, 1, 1, 0 }, /* fst.d */
    { 4, 0, 0, 1 }, /* ldptr.w */
    { 4, 1, 0, 0 }, /* stptr.w */
    { 8, 0, 0, 0 }, /* ldptr.d */
    { 8, 1, 0, 0 }, /* stptr.d */
    { 2, 0, 0, 1 }, /* ldx.h */
    { 2, 0, 0, 0 }, /* ldx.hu */
    { 2, 1, 0, 0 }, /* stx.h */
    { 4, 0, 0, 1 }, /* ldx.w */
    { 4, 0, 0, 0 }, /* ldx.wu */
    { 4, 1, 0, 0 }, /* stx.w */
    { 8, 0, 0, 0 }, /* ldx.d */
    { 8, 1, 0, 0 }, /* stx.d */
    { 4, 0, 1, 0 }, /* fldx.s */
    { 4, 1, 1, 0 }, /* fstx.s */
    { 8, 0, 1, 0 }, /* fldx.d */
    { 8, 1, 1, 0 }, /* fstx.d */
};

static uint64_t ale_instruction(unsigned int index, unsigned char *address, uint64_t value)
{
    switch (index)
    {
    case 0:
        __asm__ volatile("ld.h %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 1:
        __asm__ volatile("ld.hu %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 2:
        __asm__ volatile("st.h %0, %1, 0" : : "r"(value), "r"(address) : "memory");
        break;

    case 3:
        __asm__ volatile("ld.w %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 4:
        __asm__ volatile("ld.wu %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 5:
        __asm__ volatile("st.w %0, %1, 0" : : "r"(value), "r"(address) : "memory");
        break;

    case 6:
        __asm__ volatile("ld.d %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 7:
        __asm__ volatile("st.d %0, %1, 0" : : "r"(value), "r"(address) : "memory");
        break;

    case 8:
        __asm__ volatile("fld.s $f8, %1, 0; movfr2gr.d %0, $f8" : "=r"(value) : "r"(address) : "$f8", "memory");
        break;

    case 9:
        __asm__ volatile("movgr2fr.d $f8, %0; fst.s $f8, %1, 0" : : "r"(value), "r"(address) : "$f8", "memory");
        break;

    case 10:
        __asm__ volatile("fld.d $f8, %1, 0; movfr2gr.d %0, $f8" : "=r"(value) : "r"(address) : "$f8", "memory");
        break;

    case 11:
        __asm__ volatile("movgr2fr.d $f8, %0; fst.d $f8, %1, 0" : : "r"(value), "r"(address) : "$f8", "memory");
        break;

    case 12:
        __asm__ volatile("ldptr.w %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 13:
        __asm__ volatile("stptr.w %0, %1, 0" : : "r"(value), "r"(address) : "memory");
        break;

    case 14:
        __asm__ volatile("ldptr.d %0, %1, 0" : "=r"(value) : "r"(address) : "memory");
        break;

    case 15:
        __asm__ volatile("stptr.d %0, %1, 0" : : "r"(value), "r"(address) : "memory");
        break;

    case 16:
        __asm__ volatile("ldx.h %0, %1, $zero" : "=r"(value) : "r"(address) : "memory");
        break;

    case 17:
        __asm__ volatile("ldx.hu %0, %1, $zero" : "=r"(value) : "r"(address) : "memory");
        break;

    case 18:
        __asm__ volatile("stx.h %0, %1, $zero" : : "r"(value), "r"(address) : "memory");
        break;

    case 19:
        __asm__ volatile("ldx.w %0, %1, $zero" : "=r"(value) : "r"(address) : "memory");
        break;

    case 20:
        __asm__ volatile("ldx.wu %0, %1, $zero" : "=r"(value) : "r"(address) : "memory");
        break;

    case 21:
        __asm__ volatile("stx.w %0, %1, $zero" : : "r"(value), "r"(address) : "memory");
        break;

    case 22:
        __asm__ volatile("ldx.d %0, %1, $zero" : "=r"(value) : "r"(address) : "memory");
        break;

    case 23:
        __asm__ volatile("stx.d %0, %1, $zero" : : "r"(value), "r"(address) : "memory");
        break;

    case 24:
        __asm__ volatile("fldx.s $f8, %1, $zero; movfr2gr.d %0, $f8" : "=r"(value) : "r"(address) : "$f8", "memory");
        break;

    case 25:
        __asm__ volatile("movgr2fr.d $f8, %0; fstx.s $f8, %1, $zero" : : "r"(value), "r"(address) : "$f8", "memory");
        break;

    case 26:
        __asm__ volatile("fldx.d $f8, %1, $zero; movfr2gr.d %0, $f8" : "=r"(value) : "r"(address) : "$f8", "memory");
        break;

    case 27:
        __asm__ volatile("movgr2fr.d $f8, %0; fstx.d $f8, %1, $zero" : : "r"(value), "r"(address) : "$f8", "memory");
        break;

    }

    return value;
}

/* Exercise every saved FPR, including the ABI callee-saved bank. */
static int ale_fpr_cases(unsigned char *address)
{
    uint64_t value, expected = 0;
    unsigned int i;

    for (i = 0; i < 8; i++)
    {
        address[i] = (unsigned char)(0x81 + i * 13);
        expected |= (uint64_t)address[i] << (i * 8);
    }

    __asm__ volatile("fld.d $f0, %1, 0; movfr2gr.d %0, $f0"
                     : "=r"(value) : "r"(address) : "$f0", "memory");

    if (value != expected)
    {
        return 4000;
    }

    __asm__ volatile("fld.d $f1, %1, 0; movfr2gr.d %0, $f1"
                     : "=r"(value) : "r"(address) : "$f1", "memory");

    if (value != expected)
    {
        return 4001;
    }

    __asm__ volatile("fld.d $f2, %1, 0; movfr2gr.d %0, $f2"
                     : "=r"(value) : "r"(address) : "$f2", "memory");

    if (value != expected)
    {
        return 4002;
    }

    __asm__ volatile("fld.d $f3, %1, 0; movfr2gr.d %0, $f3"
                     : "=r"(value) : "r"(address) : "$f3", "memory");

    if (value != expected)
    {
        return 4003;
    }

    __asm__ volatile("fld.d $f4, %1, 0; movfr2gr.d %0, $f4"
                     : "=r"(value) : "r"(address) : "$f4", "memory");

    if (value != expected)
    {
        return 4004;
    }

    __asm__ volatile("fld.d $f5, %1, 0; movfr2gr.d %0, $f5"
                     : "=r"(value) : "r"(address) : "$f5", "memory");

    if (value != expected)
    {
        return 4005;
    }

    __asm__ volatile("fld.d $f6, %1, 0; movfr2gr.d %0, $f6"
                     : "=r"(value) : "r"(address) : "$f6", "memory");

    if (value != expected)
    {
        return 4006;
    }

    __asm__ volatile("fld.d $f7, %1, 0; movfr2gr.d %0, $f7"
                     : "=r"(value) : "r"(address) : "$f7", "memory");

    if (value != expected)
    {
        return 4007;
    }

    __asm__ volatile("fld.d $f8, %1, 0; movfr2gr.d %0, $f8"
                     : "=r"(value) : "r"(address) : "$f8", "memory");

    if (value != expected)
    {
        return 4008;
    }

    __asm__ volatile("fld.d $f9, %1, 0; movfr2gr.d %0, $f9"
                     : "=r"(value) : "r"(address) : "$f9", "memory");

    if (value != expected)
    {
        return 4009;
    }

    __asm__ volatile("fld.d $f10, %1, 0; movfr2gr.d %0, $f10"
                     : "=r"(value) : "r"(address) : "$f10", "memory");

    if (value != expected)
    {
        return 4010;
    }

    __asm__ volatile("fld.d $f11, %1, 0; movfr2gr.d %0, $f11"
                     : "=r"(value) : "r"(address) : "$f11", "memory");

    if (value != expected)
    {
        return 4011;
    }

    __asm__ volatile("fld.d $f12, %1, 0; movfr2gr.d %0, $f12"
                     : "=r"(value) : "r"(address) : "$f12", "memory");

    if (value != expected)
    {
        return 4012;
    }

    __asm__ volatile("fld.d $f13, %1, 0; movfr2gr.d %0, $f13"
                     : "=r"(value) : "r"(address) : "$f13", "memory");

    if (value != expected)
    {
        return 4013;
    }

    __asm__ volatile("fld.d $f14, %1, 0; movfr2gr.d %0, $f14"
                     : "=r"(value) : "r"(address) : "$f14", "memory");

    if (value != expected)
    {
        return 4014;
    }

    __asm__ volatile("fld.d $f15, %1, 0; movfr2gr.d %0, $f15"
                     : "=r"(value) : "r"(address) : "$f15", "memory");

    if (value != expected)
    {
        return 4015;
    }

    __asm__ volatile("fld.d $f16, %1, 0; movfr2gr.d %0, $f16"
                     : "=r"(value) : "r"(address) : "$f16", "memory");

    if (value != expected)
    {
        return 4016;
    }

    __asm__ volatile("fld.d $f17, %1, 0; movfr2gr.d %0, $f17"
                     : "=r"(value) : "r"(address) : "$f17", "memory");

    if (value != expected)
    {
        return 4017;
    }

    __asm__ volatile("fld.d $f18, %1, 0; movfr2gr.d %0, $f18"
                     : "=r"(value) : "r"(address) : "$f18", "memory");

    if (value != expected)
    {
        return 4018;
    }

    __asm__ volatile("fld.d $f19, %1, 0; movfr2gr.d %0, $f19"
                     : "=r"(value) : "r"(address) : "$f19", "memory");

    if (value != expected)
    {
        return 4019;
    }

    __asm__ volatile("fld.d $f20, %1, 0; movfr2gr.d %0, $f20"
                     : "=r"(value) : "r"(address) : "$f20", "memory");

    if (value != expected)
    {
        return 4020;
    }

    __asm__ volatile("fld.d $f21, %1, 0; movfr2gr.d %0, $f21"
                     : "=r"(value) : "r"(address) : "$f21", "memory");

    if (value != expected)
    {
        return 4021;
    }

    __asm__ volatile("fld.d $f22, %1, 0; movfr2gr.d %0, $f22"
                     : "=r"(value) : "r"(address) : "$f22", "memory");

    if (value != expected)
    {
        return 4022;
    }

    __asm__ volatile("fld.d $f23, %1, 0; movfr2gr.d %0, $f23"
                     : "=r"(value) : "r"(address) : "$f23", "memory");

    if (value != expected)
    {
        return 4023;
    }

    __asm__ volatile("fld.d $f24, %1, 0; movfr2gr.d %0, $f24"
                     : "=r"(value) : "r"(address) : "$f24", "memory");

    if (value != expected)
    {
        return 4024;
    }

    __asm__ volatile("fld.d $f25, %1, 0; movfr2gr.d %0, $f25"
                     : "=r"(value) : "r"(address) : "$f25", "memory");

    if (value != expected)
    {
        return 4025;
    }

    __asm__ volatile("fld.d $f26, %1, 0; movfr2gr.d %0, $f26"
                     : "=r"(value) : "r"(address) : "$f26", "memory");

    if (value != expected)
    {
        return 4026;
    }

    __asm__ volatile("fld.d $f27, %1, 0; movfr2gr.d %0, $f27"
                     : "=r"(value) : "r"(address) : "$f27", "memory");

    if (value != expected)
    {
        return 4027;
    }

    __asm__ volatile("fld.d $f28, %1, 0; movfr2gr.d %0, $f28"
                     : "=r"(value) : "r"(address) : "$f28", "memory");

    if (value != expected)
    {
        return 4028;
    }

    __asm__ volatile("fld.d $f29, %1, 0; movfr2gr.d %0, $f29"
                     : "=r"(value) : "r"(address) : "$f29", "memory");

    if (value != expected)
    {
        return 4029;
    }

    __asm__ volatile("fld.d $f30, %1, 0; movfr2gr.d %0, $f30"
                     : "=r"(value) : "r"(address) : "$f30", "memory");

    if (value != expected)
    {
        return 4030;
    }

    __asm__ volatile("fld.d $f31, %1, 0; movfr2gr.d %0, $f31"
                     : "=r"(value) : "r"(address) : "$f31", "memory");

    if (value != expected)
    {
        return 4031;
    }

    return 0;
}

/* start..start+31 must be mapped. Position it across a page boundary too. */
static int ale_run_cases(unsigned char *start)
{
    const uint64_t pattern = 0xfedcba9876543210ULL;
    unsigned int index, offset, i;

    for (index = 0; index < sizeof(ale_cases) / sizeof(ale_cases[0]); index++)
    {
        const struct ale_case *test = &ale_cases[index];

        for (offset = 1; offset < 8; offset++)
        {
            uint64_t expected = 0, result;
            unsigned char *address = start + 8 + offset;

            for (i = 0; i < 32; i++)
            {
                start[i] = (unsigned char)(i * 37 + 0x83);
            }

            for (i = 0; i < test->size; i++)
            {
                expected |= (uint64_t)address[i] << (i * 8);
            }

            if (test->sign && test->size < 8 &&
                (expected & (1ULL << (test->size * 8 - 1))))
            {
                expected |= ~0ULL << (test->size * 8);
            }

            result = ale_instruction(index, address, pattern);

            if (test->store)
            {
                for (i = 0; i < 32; i++)
                {
                    unsigned char byte = (unsigned char)(i * 37 + 0x83);

                    if (i >= 8 + offset && i < 8 + offset + test->size)
                    {
                        byte = pattern >> ((i - 8 - offset) * 8);
                    }

                    if (start[i] != byte)
                    {
                        return 1000 + index * 10 + offset;
                    }
                }
            }
            else if ((test->floating && test->size == 4) ?
                     (uint32_t)result != (uint32_t)expected : result != expected)
            {
                return 2000 + index * 10 + offset;
            }
        }
    }

    /* r0 is immutable, including a load whose value is discarded. */
    __asm__ volatile("ld.d $zero, %0, 1; st.d $zero, %0, 1"
                     : : "r"(start) : "memory");

    for (i = 1; i < 9; i++)
    {
        if (start[i])
        {
            return 3000;
        }
    }

    return ale_fpr_cases(start + 1);
}

#endif /* LOONGARCH_ALE_CASES_H */
