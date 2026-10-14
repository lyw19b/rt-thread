/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-22     LoongsonLab  fix tlb refill and page fault
 */

#include "cpuport.h"
#include "loongarch.h"
#ifdef RT_USING_SMART
#include "mmu.h"
#endif
#ifdef RT_USING_SMART
#include "tlb.h"
#endif

// Save parameters passed by loaders
unsigned long fw_arg0;
unsigned long fw_arg1;
unsigned long fw_arg2;

#ifdef RT_USING_SMART
static void mmu_init(void)
{
    unsigned long width = ARCH_PAGE_SHIFT - 3;
    unsigned long pwcl, pwch;

    write_csr_pagesize(PS_DEFAULT_SIZE);
    write_csr_stlbpgsize(PS_DEFAULT_SIZE);
    write_csr_tlbrefill_pagesize(PS_DEFAULT_SIZE);

    /* The software refill handler and hardware walker use the same 3-level
     * layout: 9-bit indices at bits 30, 21 and 12, with 64-bit entries. */
    pwcl = (12UL << MMU_PG_LEVEL_0_BASE_SHIFT) | (width << MMU_PG_LEVEL_0_WIDTH_SHIFT)
          | (21UL << MMU_PG_LEVEL_1_BASE_SHIFT) | (width << MMU_PG_LEVEL_1_WIDTH_SHIFT);
    pwch = (30UL << MMU_PG_LEVEL_3_BASE_SHIFT) | (width << MMU_PG_LEVEL_3_WIDTH_SHIFT);

#ifdef LOONGARCH_HARDWARE_PTW
    RT_ASSERT(read_cpucfg(LOONGARCH_CPUCFG2) & CPUCFG2_PTW);
    pwch |= CSR_PWCTL1_PTW;
#endif
    
    /* Writing the complete value also clears a firmware-enabled PTW bit when
     * software refill is selected. This runs separately on every CPU. */
    csr_write64(pwcl, LOONGARCH_CSR_PWCTL0);
    csr_write64(pwch, LOONGARCH_CSR_PWCTL1);
    __asm__ volatile("invtlb 0, $zero, $zero" ::: "memory");
}
#endif

void init_cpu_early(void)
{
    trap_init();
#ifdef RT_USING_SMART
    mmu_init();
#endif
}
