/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <inst.h>
#include <stack.h>
#include <stddef.h>

/* Instruction words are independent of the decoder's tables. Operand fields
 * are filled below; the effective address is supplied by the exception caller. */
static const struct
{
    rt_uint32_t word;
    rt_uint8_t bytes;
    rt_uint8_t store;
    rt_uint8_t fp;
    rt_uint8_t sign;
} memory_cases[] = {
    { 0x28400000, 2, 0, 0, 1 }, /* ld.h */
    { 0x2a400000, 2, 0, 0, 0 }, /* ld.hu */
    { 0x29400000, 2, 1, 0, 1 }, /* st.h */
    { 0x28800000, 4, 0, 0, 1 }, /* ld.w */
    { 0x2a800000, 4, 0, 0, 0 }, /* ld.wu */
    { 0x29800000, 4, 1, 0, 1 }, /* st.w */
    { 0x28c00000, 8, 0, 0, 1 }, /* ld.d */
    { 0x29c00000, 8, 1, 0, 1 }, /* st.d */
    { 0x2b000000, 4, 0, 1, 1 }, /* fld.s */
    { 0x2b400000, 4, 1, 1, 1 }, /* fst.s */
    { 0x2b800000, 8, 0, 1, 1 }, /* fld.d */
    { 0x2bc00000, 8, 1, 1, 1 }, /* fst.d */
    { 0x24000000, 4, 0, 0, 1 }, /* ldptr.w */
    { 0x25000000, 4, 1, 0, 1 }, /* stptr.w */
    { 0x26000000, 8, 0, 0, 1 }, /* ldptr.d */
    { 0x27000000, 8, 1, 0, 1 }, /* stptr.d */
    { 0x38040000, 2, 0, 0, 1 }, /* ldx.h */
    { 0x38240000, 2, 0, 0, 0 }, /* ldx.hu */
    { 0x38140000, 2, 1, 0, 1 }, /* stx.h */
    { 0x38080000, 4, 0, 0, 1 }, /* ldx.w */
    { 0x38280000, 4, 0, 0, 0 }, /* ldx.wu */
    { 0x38180000, 4, 1, 0, 1 }, /* stx.w */
    { 0x380c0000, 8, 0, 0, 1 }, /* ldx.d */
    { 0x381c0000, 8, 1, 0, 1 }, /* stx.d */
    { 0x38300000, 4, 0, 1, 1 }, /* fldx.s */
    { 0x38380000, 4, 1, 1, 1 }, /* fstx.s */
    { 0x38340000, 8, 0, 1, 1 }, /* fldx.d */
    { 0x383c0000, 8, 1, 1, 1 }, /* fstx.d */
};

/* Preserve the selected live FPR, including ABI callee-saved registers. No
 * floating-point C expression may be placed between these assembly accesses. */
static rt_uint64_t emulate_fpr(struct pt_regs *regs, void *addr, rt_uint32_t *pc,
                               unsigned int reg, rt_uint64_t initial)
{
    rt_uint64_t saved, result = 0;

#define FPR_CASE(n)                                                           \
    case n:                                                                   \
        __asm__ volatile("movfr2gr.d %0, $f" #n : "=r"(saved));               \
        __asm__ volatile("movgr2fr.d $f" #n ", %0" ::"r"(initial) : "$f" #n); \
        emulate_load_store_insn(regs, addr, pc);                              \
        __asm__ volatile("movfr2gr.d %0, $f" #n : "=r"(result));              \
        __asm__ volatile("movgr2fr.d $f" #n ", %0" ::"r"(saved) : "$f" #n);   \
        break

    switch (reg)
    {
        FPR_CASE(0);
        FPR_CASE(1);
        FPR_CASE(2);
        FPR_CASE(3);
        FPR_CASE(4);
        FPR_CASE(5);
        FPR_CASE(6);
        FPR_CASE(7);
        FPR_CASE(8);
        FPR_CASE(9);
        FPR_CASE(10);
        FPR_CASE(11);
        FPR_CASE(12);
        FPR_CASE(13);
        FPR_CASE(14);
        FPR_CASE(15);
        FPR_CASE(16);
        FPR_CASE(17);
        FPR_CASE(18);
        FPR_CASE(19);
        FPR_CASE(20);
        FPR_CASE(21);
        FPR_CASE(22);
        FPR_CASE(23);
        FPR_CASE(24);
        FPR_CASE(25);
        FPR_CASE(26);
        FPR_CASE(27);
        FPR_CASE(28);
        FPR_CASE(29);
        FPR_CASE(30);
        FPR_CASE(31);
    }
#undef FPR_CASE
    return result;
}

static rt_uint64_t expected_load(const rt_uint8_t *data, unsigned int bytes, int sign)
{
    rt_uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < bytes; i++)
    {
        value |= (rt_uint64_t)data[i] << (8 * i);
    }
    if (sign && bytes < 8 && (data[bytes - 1] & 0x80))
    {
        value |= ~0ULL << (8 * bytes);
    }
    return value;
}

static int port_unaligned_test(void)
{
    const rt_uint64_t initial = 0xfedcba9876543210ULL;
    struct pt_regs regs, expected;
    rt_uint8_t data[24], wanted[24];
    rt_uint64_t value;
    unsigned int size, sign, offset, pattern, index, reg, i;
    unsigned int checks = 0;
    struct loongarch_mem_access access, untouched;
    static const rt_uint32_t unsupported[] = {
        0, 0xffffffff, 0x28000000, 0x29000000, /* invalid, ld.b, st.b */
        0x20000000, 0x21000000, 0x22000000, 0x23000000, /* ll/sc */
        0x38000000, 0x38100000, 0x002a0000 /* ldxb, stxb, break */
    };

#define VERIFY(expr)                                                \
    do                                                              \
    {                                                               \
        if (!(expr))                                                \
        {                                                           \
            rt_kprintf("UNALIGNED TEST: FAIL line=%d\n", __LINE__); \
            return -RT_ERROR;                                       \
        }                                                           \
    } while (0)

    VERIFY(sizeof(struct pt_regs) == PT_SIZE);
    VERIFY(offsetof(struct pt_regs, r_s8) == PT_R31);
    VERIFY(offsetof(struct pt_regs, r_era) == PT_ERA);
    VERIFY(offsetof(struct pt_regs, f) == PT_FPR0);
    VERIFY(offsetof(struct pt_regs, fcsr) == PT_FCSR);
    VERIFY(offsetof(struct pt_regs, fcc) == PT_FCC);
    VERIFY(sizeof(struct rt_hw_switch_frame) == RT_THREAD_END);
    VERIFY(offsetof(struct rt_hw_switch_frame, f) == RT_THREAD_FPR24);
    VERIFY(offsetof(struct rt_hw_switch_frame, r_fcsr) == RT_THREAD_FCSR);
    VERIFY(offsetof(struct rt_hw_switch_frame, r_fcc) == RT_THREAD_FCC);

    rt_memset(&untouched, 0x5a, sizeof(untouched));
    for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
    {
        access = untouched;
        VERIFY(!loongarch_decode_mem_access(unsupported[i], &access));
        VERIFY(rt_memcmp(&access, &untouched, sizeof(access)) == 0);
    }

    value = initial;
    VERIFY(unaligned_read(data, &value, 0, 0) == (unsigned long)-1);
    VERIFY(value == initial);
    rt_memset(data, 0x5a, sizeof(data));
    rt_memcpy(wanted, data, sizeof(data));
    VERIFY(unaligned_write(data, initial, 0) == (unsigned long)-1);
    VERIFY(rt_memcmp(data, wanted, sizeof(data)) == 0);

    for (pattern = 0; pattern < 2; pattern++)
    {
        for (offset = 1; offset < 8; offset++)
        {
            for (size = 1; size <= 8; size++)
            {
                for (i = 0; i < sizeof(data); i++)
                {
                    data[i] = pattern ? 0x80 + i : i;
                }
                for (sign = 0; sign < 2; sign++)
                {
                    VERIFY(unaligned_read(data + offset, &value, size, sign) == 0);
                    VERIFY(value == expected_load(data + offset, size, sign));
                    checks++;
                }
                rt_memcpy(wanted, data, sizeof(data));
                for (i = 0; i < size; i++)
                {
                    wanted[offset + i] = initial >> (8 * i);
                }
                VERIFY(unaligned_write(data + offset, initial, size) == 0);
                VERIFY(rt_memcmp(data, wanted, sizeof(data)) == 0);
                checks++;
            }

            for (index = 0; index < sizeof(memory_cases) / sizeof(memory_cases[0]); index++)
            {
                for (reg = 0; reg < 32; reg++)
                {
                    /* Exercise ignored rj/rk/immediate bits as well as Rd. */
                    rt_uint32_t word = memory_cases[index].word | (pattern ? 0x7fe0 : 0) | reg;
                    rt_ubase_t *gpr = (rt_ubase_t *)&regs;
                    rt_ubase_t *expected_gpr = (rt_ubase_t *)&expected;
                    rt_uint64_t fpr = initial;

                    for (i = 0; i < sizeof(data); i++)
                    {
                        data[i] = pattern ? 0x80 + i : i;
                    }
                    rt_memcpy(wanted, data, sizeof(data));
                    rt_memset(&regs, 0x5a, sizeof(regs));
                    gpr[reg] = initial;
                    regs.r_era = 0x1000;
                    rt_memcpy(&expected, &regs, sizeof(regs));
                    expected.r_era += 4;
                    size = memory_cases[index].bytes;
                    if (memory_cases[index].store)
                    {
                        for (i = 0; i < size; i++)
                        {
                            wanted[offset + i] = initial >> (8 * i);
                        }
                    }
                    else
                    {
                        value = expected_load(data + offset, size, memory_cases[index].sign);
                        if (memory_cases[index].fp)
                        {
                            fpr = value;
                        }
                        else
                        {
                            expected_gpr[reg] = value;
                        }
                    }
                    if (memory_cases[index].fp)
                    {
                        VERIFY(emulate_fpr(&regs, data + offset, &word, reg, initial) == fpr);
                    }
                    else
                    {
                        emulate_load_store_insn(&regs, data + offset, &word);
                    }
                    VERIFY(rt_memcmp(data, wanted, sizeof(data)) == 0);
                    VERIFY(rt_memcmp(&regs, &expected, sizeof(regs)) == 0);
                    checks++;
                }
            }
        }
    }
#undef VERIFY
    rt_kprintf("UNALIGNED TEST: PASS (%u cases)\n", checks);
    return RT_EOK;
}
MSH_CMD_EXPORT(port_unaligned_test, Check LoongArch memory instruction emulation);
