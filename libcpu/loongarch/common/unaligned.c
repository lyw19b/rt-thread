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
#include <rthw.h>
#include <loongarch.h>

/* Explicit helper calls below retain their live-FPR interface. ALE recovery
 * instead updates the saved frame and validates the complete memory range
 * before using byte accesses, which have no nested-fault fixup. */

struct memory_opcode
{
    rt_uint32_t opcode;
    rt_uint8_t operand_bits;
    rt_uint8_t size;
    rt_uint8_t flags;
};

static rt_ubase_t recovered_ale[2];

rt_ubase_t loongarch_ale_count(rt_bool_t user)
{
    return __atomic_load_n(&recovered_ale[user != 0], __ATOMIC_RELAXED);
}

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

/** @brief Deny implicit recovery until the BSP supplies its stable RAM policy. */
rt_weak rt_bool_t loongarch_unaligned_access_ok(rt_ubase_t address, rt_size_t size,
                                              rt_bool_t write, rt_bool_t instruction)
{
    RT_UNUSED(address);
    RT_UNUSED(size);
    RT_UNUSED(write);
    RT_UNUSED(instruction);

    return RT_FALSE;
}

/**
 * @brief Transfer validated scalar bytes without taking a nested memory fault.
 * @note User copies may sleep; their caller restores the interrupted IRQ state.
 */
static rt_bool_t ale_copy(struct pt_regs *regs, void *buffer, rt_ubase_t address,
                         rt_size_t size, rt_bool_t write, rt_bool_t instruction)
{
    rt_size_t i;

    if (regs->r_prmd & 3)
    {
#ifdef RT_USING_SMART
        return loongarch_unaligned_user_copy(buffer, address, size, write, instruction);
#else
        return RT_FALSE;
#endif
    }

    if (!loongarch_unaligned_access_ok(address, size, write, instruction))
    {
        return RT_FALSE;
    }

    for (i = 0; i < size; i++)
    {
        if (write)
        {
            ((volatile rt_uint8_t *)address)[i] = ((rt_uint8_t *)buffer)[i];
        }
        else
        {
            ((rt_uint8_t *)buffer)[i] = ((volatile rt_uint8_t *)address)[i];
        }
    }

    return RT_TRUE;
}

/**
 * @brief Recover a scalar ALE using the complete saved exception context.
 * @note MMIO, atomic/vector instructions and invalid mappings are rejected.
 *       A failed transfer leaves the saved destination and ERA unchanged.
 *       FPR results belong in the frame: exception return restores all FPRs.
 */
rt_bool_t loongarch_try_emulate_ale(struct pt_regs *regs)
{
    struct loongarch_mem_access access;
    rt_ubase_t *gpr = (rt_ubase_t *)regs;
    rt_uint64_t value = 0;
    rt_uint32_t word;
    rt_bool_t store, success = RT_FALSE;
    rt_bool_t user = (regs->r_prmd & 3) != 0;

    if (regs->r_era & 3)
    {
        return RT_FALSE;
    }

    if (user)
    {
        if (!(regs->r_prmd & CSR_PRMD_PIE) || !rt_scheduler_is_available())
        {
            return RT_FALSE;
        }

        rt_hw_local_irq_enable(regs->r_prmd & CSR_PRMD_PIE);
    }

    if (!ale_copy(regs, &word, regs->r_era, sizeof(word), RT_FALSE, RT_TRUE) ||
        !loongarch_decode_mem_access(word, &access))
    {
        goto out;
    }

    store = (access.flags & LOONGARCH_MEM_STORE) != 0;

    /* Kernel r21 is the live per-CPU base and is not restored from pt_regs.
     * A store can read it here; treating a load as recovered would lose its
     * result. User r21, like the other user GPRs, lives in the saved frame. */
    if (!(regs->r_bvaddr & (access.size - 1)) ||
        (!user && !store && !(access.flags & LOONGARCH_MEM_FLOAT) && access.reg == 21))
    {
        goto out;
    }

    if (store)
    {
        if (access.flags & LOONGARCH_MEM_FLOAT)
        {
            value = regs->f[access.reg];
        }
        else if (!user && access.reg == 21)
        {
            __asm__ volatile("move %0, $r21" : "=r"(value));
        }
        else
        {
            value = access.reg ? gpr[access.reg] : 0;
        }
    }

    if (!ale_copy(regs, &value, regs->r_bvaddr, access.size, store, RT_FALSE))
    {
        goto out;
    }

    if (!store)
    {
        if ((access.flags & LOONGARCH_MEM_SIGNED) && access.size < 8 &&
            (value & (1ULL << (access.size * 8 - 1))))
        {
            value |= ~0ULL << (access.size * 8);
        }

        if (access.flags & LOONGARCH_MEM_FLOAT)
        {
            regs->f[access.reg] = value;
        }
        else if (access.reg)
        {
            gpr[access.reg] = value;
        }
    }

    regs->r_era += sizeof(word);
    __atomic_fetch_add(&recovered_ale[user], 1, __ATOMIC_RELAXED);
    success = RT_TRUE;

out:
    if (user)
    {
        rt_hw_local_irq_disable();
    }

    return success;
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
