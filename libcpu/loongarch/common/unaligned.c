/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#include <rtthread.h>
#include <inst.h>
#include <stack.h>

/* The emulator accesses live FPRs and stops on unsupported instructions.
 * Memory faults are not recoverable here. do_ale() keeps its separate fatal
 * exception policy; callers invoke this helper explicitly. */

struct memory_opcode
{
    rt_uint32_t opcode;
    rt_uint8_t operand_bits;
    rt_uint8_t size;
    rt_uint8_t flags;
};

/* Compare only the opcode: BADV already supplies the effective address. */
static const struct memory_opcode memory_opcodes[] = {
    { 0xa1, 22, 2, LOONGARCH_MEM_SIGNED }, /* ld.h */
    { 0xa9, 22, 2, 0 }, /* ld.hu */
    { 0xa5, 22, 2, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* st.h */
    { 0xa2, 22, 4, LOONGARCH_MEM_SIGNED }, /* ld.w */
    { 0xaa, 22, 4, 0 }, /* ld.wu */
    { 0xa6, 22, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* st.w */
    { 0xa3, 22, 8, LOONGARCH_MEM_SIGNED }, /* ld.d */
    { 0xa7, 22, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* st.d */
    { 0xac, 22, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_FLOAT }, /* fld.s */
    { 0xad, 22, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE | LOONGARCH_MEM_FLOAT }, /* fst.s */
    { 0xae, 22, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_FLOAT }, /* fld.d */
    { 0xaf, 22, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE | LOONGARCH_MEM_FLOAT }, /* fst.d */
    { 0x24, 24, 4, LOONGARCH_MEM_SIGNED }, /* ldptr.w */
    { 0x25, 24, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* stptr.w */
    { 0x26, 24, 8, LOONGARCH_MEM_SIGNED }, /* ldptr.d */
    { 0x27, 24, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* stptr.d */
    { 0x7008, 15, 2, LOONGARCH_MEM_SIGNED }, /* ldx.h */
    { 0x7048, 15, 2, 0 }, /* ldx.hu */
    { 0x7028, 15, 2, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* stx.h */
    { 0x7010, 15, 4, LOONGARCH_MEM_SIGNED }, /* ldx.w */
    { 0x7050, 15, 4, 0 }, /* ldx.wu */
    { 0x7030, 15, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* stx.w */
    { 0x7018, 15, 8, LOONGARCH_MEM_SIGNED }, /* ldx.d */
    { 0x7038, 15, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE }, /* stx.d */
    { 0x7060, 15, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_FLOAT }, /* fldx.s */
    { 0x7070, 15, 4, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE | LOONGARCH_MEM_FLOAT }, /* fstx.s */
    { 0x7068, 15, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_FLOAT }, /* fldx.d */
    { 0x7078, 15, 8, LOONGARCH_MEM_SIGNED | LOONGARCH_MEM_STORE | LOONGARCH_MEM_FLOAT }, /* fstx.d */
};

/**
 * @brief Decode a supported scalar memory instruction without C bitfields.
 * @param word Instruction word at the faulting PC.
 * @param access Receives the register, byte count and access flags on success.
 * @return RT_TRUE for a supported instruction; otherwise access is unchanged.
 */
rt_bool_t loongarch_decode_mem_access(rt_uint32_t word, struct loongarch_mem_access *access)
{
    rt_size_t i;

    for (i = 0; i < sizeof(memory_opcodes) / sizeof(memory_opcodes[0]); i++)
    {
        const struct memory_opcode *entry = &memory_opcodes[i];

        if ((word >> entry->operand_bits) == entry->opcode)
        {
            access->reg = word & 0x1f;
            access->size = entry->size;
            access->flags = entry->flags;
            return RT_TRUE;
        }
    }
    return RT_FALSE;
}

/* FPR operands must be assembler constants. Share the register list between
 * the two access directions; these helpers deliberately access live FPRs. */
/* clang-format off */
#define FOR_EACH_FPR(operation) \
    operation(0) operation(1) operation(2) operation(3) \
    operation(4) operation(5) operation(6) operation(7) \
    operation(8) operation(9) operation(10) operation(11) \
    operation(12) operation(13) operation(14) operation(15) \
    operation(16) operation(17) operation(18) operation(19) \
    operation(20) operation(21) operation(22) operation(23) \
    operation(24) operation(25) operation(26) operation(27) \
    operation(28) operation(29) operation(30) operation(31)
/* clang-format on */

static rt_uint64_t read_fpr(rt_uint8_t reg)
{
    rt_uint64_t value = 0;

#define READ_FPR_CASE(n)                                        \
    case n:                                                     \
        __asm__ volatile("movfr2gr.d %0, $f" #n : "=r"(value)); \
        break;
    switch (reg)
    {
        FOR_EACH_FPR(READ_FPR_CASE)
    }
#undef READ_FPR_CASE
    return value;
}

static void write_fpr(rt_uint8_t reg, rt_uint64_t value)
{
#define WRITE_FPR_CASE(n)                                         \
    case n:                                                       \
        __asm__ volatile("movgr2fr.d $f" #n ", %0" ::"r"(value)); \
        break;
    switch (reg)
    {
        FOR_EACH_FPR(WRITE_FPR_CASE)
    }
#undef WRITE_FPR_CASE
}
#undef FOR_EACH_FPR

/**
 * @brief Emulate one scalar load or store at a caller-supplied address.
 * @param regs Saved integer context; ERA advances only after a successful access.
 * @param addr Effective address, already computed by the CPU or caller.
 * @param pc Address of the instruction word to decode.
 * @note The caller must supply readable instruction/data memory and keep the
 *       live FPR bank stable. Unsupported instructions stop here as before.
 */
void emulate_load_store_insn(struct pt_regs *regs, void *addr, rt_uint32_t *pc)
{
    struct loongarch_mem_access access;
    rt_ubase_t *gpr = (rt_ubase_t *)regs;
    rt_uint64_t value;
    unsigned long result;

    if (!loongarch_decode_mem_access(*pc, &access))
    {
        goto fault;
    }

    if (access.flags & LOONGARCH_MEM_STORE)
    {
        value = (access.flags & LOONGARCH_MEM_FLOAT) ? read_fpr(access.reg) : gpr[access.reg];
        result = unaligned_write(addr, value, access.size);
    }
    else
    {
        result = unaligned_read(addr, &value, access.size, access.flags & LOONGARCH_MEM_SIGNED);
        if (result == 0)
        {
            if (access.flags & LOONGARCH_MEM_FLOAT)
            {
                write_fpr(access.reg, value);
            }
            else
            {
                gpr[access.reg] = value;
            }
        }
    }

    if (result == 0)
    {
        regs->r_era += sizeof(*pc);
        return;
    }

fault:
    rt_kprintf("Unhandled unaligned access");
    while (1)
    {
    }
}
