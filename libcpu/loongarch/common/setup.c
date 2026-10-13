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
void mmu_tlb_init() {
	write_csr_pagesize(PS_DEFAULT_SIZE);
	write_csr_stlbpgsize(PS_DEFAULT_SIZE);
	write_csr_tlbrefill_pagesize(PS_DEFAULT_SIZE);
}


void mmu_ptw_init() {

	// three level page table.
	unsigned long pwcl = 0;
	unsigned long pwch = 0;

	unsigned long pgd_idx = 30;
	unsigned long pgd_wdt = (ARCH_PAGE_SHIFT - 3);

	unsigned long pmd_idx = 21;
	unsigned long pmd_wdt = (ARCH_PAGE_SHIFT - 3);

	unsigned long pte_idx = 12;
	unsigned long pte_wdt = (ARCH_PAGE_SHIFT - 3);

	// set csr PWCL
	pwcl =  (pte_idx << MMU_PG_LEVEL_0_BASE_SHIFT) | (pte_wdt << MMU_PG_LEVEL_0_WIDTH_SHIFT)  // PTE
	      | (pmd_idx << MMU_PG_LEVEL_1_BASE_SHIFT) | (pmd_wdt << MMU_PG_LEVEL_1_WIDTH_SHIFT)  // PMD
	      | (MMU_PG_LEVEL_PTE_WIDTH_64 << MMU_PG_LEVEL_PTE_WIDTH_SHIFT); // PTE size

	// set csr PWCH
	pwch = (pgd_idx << MMU_PG_LEVEL_3_BASE_SHIFT) | (pgd_wdt << MMU_PG_LEVEL_3_WIDTH_SHIFT);

#ifdef LOONGARCH_HARDWARE_PTW
	// enable hardware ptw
	pwch |= CSR_PWCTL1_PTW;
#endif

	csr_write64(pwcl, LOONGARCH_CSR_PWCTL0);
	csr_write64(pwch, LOONGARCH_CSR_PWCTL1);


	__asm__ __volatile__ ("invtlb 0, $zero, $zero\n\t" :::"memory");

}
#endif

void init_cpu_early(void)
{
	trap_init();
#ifdef RT_USING_SMART
	mmu_tlb_init();
	mmu_ptw_init();
#endif
}
