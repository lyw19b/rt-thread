/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <rttypes.h>

#include "loongarch.h"

/* Keep the compiler barrier paired with the CRMD.IE exchange in both paths. */
static inline void rt_hw_arch_local_irq_update(rt_uint32_t flags)
{
    __asm__ volatile(
        "csrxchg %0, %1, %2"
        : "+r"(flags)
        : "r"(CSR_CRMD_IE), "i"(LOONGARCH_CSR_CRMD)
        : "memory");
}

static inline void rt_hw_arch_local_irq_enable(void)
{
    rt_hw_arch_local_irq_update(CSR_CRMD_IE);
}

static inline void rt_hw_arch_local_irq_disable(void)
{
    rt_hw_arch_local_irq_update(0);
}


#endif /* _ASM_IRQ_H */
