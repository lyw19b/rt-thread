/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               standardize file header
 */

/*
 * Description:
 *   LoongArch inst.
 */

/*
 * LoongArch scalar memory instruction interface.
 *
 * Describes the scalar accesses supported by the RT-Thread emulator.
 * Effective addresses come from the caller; only opcode and Rd are decoded.
 */

#ifndef __ASM_INST_H__
#define __ASM_INST_H__

#include <rttypes.h>

#define LOONGARCH_MEM_SIGNED 0x01
#define LOONGARCH_MEM_STORE  0x02
#define LOONGARCH_MEM_FLOAT  0x04

struct loongarch_mem_access
{
    rt_uint8_t reg;
    rt_uint8_t size;
    rt_uint8_t flags;
};

struct pt_regs;

rt_bool_t loongarch_decode_mem_access(rt_uint32_t word, struct loongarch_mem_access *access);
unsigned long unaligned_read(void *addr, void *value, unsigned long n, int sign);
unsigned long unaligned_write(void *addr, unsigned long value, unsigned long n);
void emulate_load_store_insn(struct pt_regs *regs, void *addr, rt_uint32_t *pc);

/* Exception recovery is restricted to memory explicitly approved by the BSP. */
rt_bool_t loongarch_unaligned_access_ok(rt_ubase_t address, rt_size_t size,
                                      rt_bool_t write, rt_bool_t instruction);
rt_bool_t loongarch_try_emulate_ale(struct pt_regs *regs);
rt_ubase_t loongarch_ale_count(rt_bool_t user);

#ifdef RT_USING_SMART
rt_bool_t loongarch_unaligned_user_copy(void *buffer, rt_ubase_t address,
                                      rt_size_t size, rt_bool_t write,
                                      rt_bool_t instruction);
#endif

#endif /* __ASM_INST_H__ */
