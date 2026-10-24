/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-16     LoongsonLab  add and fix page table attributes
 * 2026-10-23     OpenLANPU    Define read-only Smart user mappings
 */

#ifndef _ASM_MMU_H
#define _ASM_MMU_H


#include <mm_aspace.h>
#include <mm_page.h>
#include "kernel_map.h"

struct mem_desc
{
    rt_size_t vaddr_start;
    rt_size_t vaddr_end;
    rt_ubase_t paddr_start;
    rt_size_t attr;
    struct rt_varea varea;
};


/* RAM, Flash, or ROM */
#define NORMAL_MEM 0
/* normal nocache memory mapping type */
#define NORMAL_NOCACHE_MEM 1
/* MMIO region */
#define DEVICE_MEM 2

#define ARCH_MAP_FAILED ((void *)-1)


#define PAGE_OFFSET_BIT  12
#define ARCH_PAGE_SHIFT  PAGE_OFFSET_BIT
#define ARCH_PAGE_SIZE   (1ULL << ARCH_PAGE_SHIFT)
#define ARCH_PAGE_MASK   (ARCH_PAGE_SIZE - 1)

#define MMU_PG_LEVEL 3
#define ARCH_PAGE_LEVEL_SHIFT (ARCH_PAGE_SHIFT - 3)
#define ARCH_PAGE_LEVEL_MASK  ((1 << ARCH_PAGE_LEVEL_SHIFT) - 1)
#define ARCH_PAGE_ADDRESS_MASK 0x0000fffffffff000UL

#define MMU_PG_LEVEL_3_VA_WIDTH  (39)
#define MMU_PG_LEVEL_3_SHIFT     (MMU_PG_LEVEL_3_VA_WIDTH - ARCH_PAGE_LEVEL_SHIFT)
#define MMU_PG_LEVEL_2_SHIFT     (MMU_PG_LEVEL_3_SHIFT - ARCH_PAGE_LEVEL_SHIFT)
#define MMU_PG_LEVEL_1_SHIFT     (MMU_PG_LEVEL_2_SHIFT - ARCH_PAGE_LEVEL_SHIFT)


#define ARCH_PAGE_PA_MASK(x) (LOONGARCH_ALIAS_PHYS(x) & ARCH_PAGE_ADDRESS_MASK)
#define ARCH_PAGE_VA_MASK(x) LOONGARCH_PHYS_CACHED(x)

#define ARCH_PAGE_PA2VA(x) LOONGARCH_PHYS_CACHED(x)

#define RT_HW_MMU_PROT_READ    1
#define RT_HW_MMU_PROT_WRITE   2
#define RT_HW_MMU_PROT_EXECUTE 4
#define RT_HW_MMU_PROT_KERNEL  8
#define RT_HW_MMU_PROT_USER    16
#define RT_HW_MMU_PROT_CACHE   32


#define ARCH_ADDRESS_WIDTH_BITS 64

#define PHYSICAL_ADDRESS_WIDTH_BITS 56

/* Page metadata covers the low 4 GiB RAM window, not the 39-bit user VA. */
#define ARCH_VADDR_WIDTH 32


/* Page table bits */
#define MMU_PAGE_VALID_SHIFT   0
#define MMU_PAGE_ACCESSED_SHIFT    0  /* Reuse Valid for Accessed */
#define MMU_PAGE_DIRTY_SHIFT   1
#define MMU_PAGE_PLV_SHIFT     2  /* 2~3, two bits */
#define MMU_CACHE_SHIFT        4  /* 4~5, two bits */
#define MMU_PAGE_GLOBAL_SHIFT  6
#define MMU_PAGE_HUGE_SHIFT    6  /* HUGE is a PMD bit */
#define MMU_PAGE_PRESENT_SHIFT 7
#define MMU_PAGE_WRITE_SHIFT   8
#define MMU_PAGE_MODIFIED_SHIFT    9
#define MMU_PAGE_PROTNONE_SHIFT    10
#define MMU_PAGE_SPECIAL_SHIFT 11
#define MMU_PAGE_HGLOBAL_SHIFT 12 /* HGlobal is a PMD bit */

#define MMU_PAGE_NO_READ_SHIFT 61
#define MMU_PAGE_NO_EXEC_SHIFT 62
#define MMU_PAGE_RPLV_SHIFT    63

#define PLV_KERN            0
#define PLV_USER            3
#define PLV_MASK            0x3

#define MMU_PAGE_USER     (PLV_USER << MMU_PAGE_PLV_SHIFT)
#define MMU_PAGE_KERN     (PLV_KERN << MMU_PAGE_PLV_SHIFT)

#define MMU_CACHE_SUC     (0 << MMU_CACHE_SHIFT) /* Strong-ordered UnCached */
#define MMU_CACHE_CC      (1 << MMU_CACHE_SHIFT) /* Coherent Cached */

#define MMU_PAGE_DIRTY    (1 << MMU_PAGE_DIRTY_SHIFT)
#define MMU_PAGE_PRESENT  (1 << MMU_PAGE_PRESENT_SHIFT)
#define MMU_PAGE_VALID    (1 << MMU_PAGE_VALID_SHIFT)
#define MMU_PAGE_ACCESSED (1 << MMU_PAGE_ACCESSED_SHIFT)
#define MMU_PAGE_WRITE    (1 << MMU_PAGE_WRITE_SHIFT)
#define MMU_PAGE_NO_EXEC  (1UL << MMU_PAGE_NO_EXEC_SHIFT)
#define MMU_PAGE_GLOBAL   (1 << MMU_PAGE_GLOBAL_SHIFT)

#define MMU_PAGE_ATTR_EXE (1 << MMU_PAGE_SPECIAL_SHIFT)

#define ARCH_PAGE_BIT_V     MMU_PAGE_VALID

#define MMU_MAP_K_DEVICE   (MMU_PAGE_KERN | MMU_PAGE_GLOBAL | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_SUC)
#define MMU_MAP_K_RWCB     (MMU_PAGE_KERN | MMU_PAGE_GLOBAL | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_CC)
#define MMU_MAP_K_RW       (MMU_PAGE_KERN | MMU_PAGE_GLOBAL | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_SUC)
#define MMU_MAP_U_ROCB     (MMU_PAGE_USER | MMU_PAGE_VALID | MMU_CACHE_CC)
#define MMU_MAP_U_RWCB     (MMU_PAGE_USER | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_CC)
#define MMU_MAP_U_RWCB_XN  (MMU_PAGE_USER | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_CC | MMU_PAGE_NO_EXEC)
#define MMU_MAP_U_RW       (MMU_PAGE_USER | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_SUC)
#define MMU_MAP_EARLY      (MMU_PAGE_KERN | MMU_PAGE_GLOBAL | MMU_PAGE_VALID | MMU_PAGE_WRITE | MMU_CACHE_CC)

#define MMU_DEFAULT_USER_ATTR  MMU_MAP_U_RWCB


#ifdef RT_USING_SMART

rt_inline void rt_hw_tlb_invalidate_all_local(void)
{
    __asm__ volatile("invtlb 0x0, $zero, $zero" ::: "memory");
}

#ifdef RT_USING_SMP
void loongarch_tlb_flush_all(void);
#else
#define loongarch_tlb_flush_all rt_hw_tlb_invalidate_all_local
#endif

rt_inline void rt_hw_tlb_invalidate_range(rt_aspace_t aspace, void *start,
                                        size_t size, size_t stride)
{
    RT_UNUSED(aspace);
    RT_UNUSED(start);
    RT_UNUSED(size);
    RT_UNUSED(stride);
    loongarch_tlb_flush_all();
}

// MMU related functions.
void *rt_hw_mmu_pgtbl_create(void);
void rt_hw_mmu_pgtbl_delete(void *pgtbl);

int rt_hw_mmu_map_init(rt_aspace_t aspace, void *v_address, rt_ubase_t size,
                       rt_ubase_t *vtable, rt_ubase_t pv_off);
void rt_hw_mmu_kernel_map_init(rt_aspace_t aspace, rt_ubase_t vaddr_start,
                               rt_ubase_t size);

void rt_hw_mmu_setup(rt_aspace_t aspace, struct mem_desc *mdesc, int desc_nr);
int rt_hw_mmu_control(struct rt_aspace *aspace, void *vaddr, size_t size,
                      enum rt_mmu_cntl cmd);

void *rt_hw_mmu_map(rt_aspace_t aspace, void *v_addr, void *p_addr, size_t size,
                    size_t attr);
void rt_hw_mmu_unmap(rt_aspace_t aspace, void *v_addr, size_t size);

void rt_hw_aspace_switch(rt_aspace_t aspace);

void *rt_hw_mmu_v2p(rt_aspace_t aspace, void *vaddr);

void *rt_hw_mmu_tbl_get(void);

int rt_hw_mmu_update_modify_page(rt_aspace_t aspace, rt_ubase_t *vaddr);

#endif

/**
 * @brief Remove permission from attribution
 *
 * @param attr architecture specified mmu attribution
 * @param prot protect that will be removed
 * @return size_t returned attribution
 */
rt_inline size_t rt_hw_mmu_attr_rm_perm(size_t attr, rt_base_t prot)
{
    switch (prot)
    {
        /* remove write permission for user */
        case RT_HW_MMU_PROT_WRITE | RT_HW_MMU_PROT_USER:
            attr &= ~(MMU_PAGE_WRITE | MMU_PAGE_DIRTY);
            break;
        /* remove write permission for kernel */
        case RT_HW_MMU_PROT_WRITE | RT_HW_MMU_PROT_KERNEL:
            attr &= ~(MMU_PAGE_WRITE | MMU_PAGE_DIRTY);
            break;
        default:
            RT_ASSERT(0);
    }
    return attr;
}

/**
 * @brief Add permission from attribution
 *
 * @param attr architecture specified mmu attribution
 * @param prot protect that will be added
 * @return size_t returned attribution
 */
rt_inline size_t rt_hw_mmu_attr_add_perm(size_t attr, rt_base_t prot)
{
    switch (prot)
    {
        /* add write permission for user */
        case RT_HW_MMU_PROT_WRITE | RT_HW_MMU_PROT_USER:
            attr |= (MMU_PAGE_USER | MMU_PAGE_VALID | MMU_PAGE_WRITE);
            break;
        default:
            RT_ASSERT(0);
    }
    return attr;
}


/**
 * @brief Test permission from attribution
 *
 * @param attr architecture specified mmu attribution
 * @param prot protect that will be test
 * @return rt_bool_t RT_TRUE if the prot is allowed, otherwise RT_FALSE
 */
rt_inline rt_bool_t rt_hw_mmu_attr_test_perm(size_t attr, rt_base_t prot)
{
    rt_bool_t rc = 0;
    switch (prot & ~RT_HW_MMU_PROT_USER)
    {
        /* test write permission for user */
        case RT_HW_MMU_PROT_WRITE:
            rc = attr & MMU_PAGE_WRITE;
            break;
        case RT_HW_MMU_PROT_READ:
            rc = attr & MMU_PAGE_ACCESSED;
            break;
         case RT_HW_MMU_PROT_EXECUTE:
            rc = !(attr & MMU_PAGE_NO_EXEC);
            break;
        default:
            RT_ASSERT(0);
    }

    if (rc && (prot & RT_HW_MMU_PROT_USER))
    {
        rc = attr & MMU_PAGE_USER;
    }
    return rc;
}

#define MMU_MAP_ERROR_VANOTALIGN -1
#define MMU_MAP_ERROR_PANOTALIGN -2
#define MMU_MAP_ERROR_NOPAGE     -3
#define MMU_MAP_ERROR_CONFLICT   -4


#ifdef KMD_USE_PAGE
/* The fixed RAM/device windows are outside the dynamic aspace allocator. */
#define KERNEL_VADDR_SIZE  (0x2000000000UL - ARCH_PAGE_SIZE)
#else
#define KERNEL_VADDR_SIZE  (0x4000000000UL - ARCH_PAGE_SIZE)
#endif
#define KERNEL_VADDR_END   (0xffffffc000000000UL + KERNEL_VADDR_SIZE)

#endif /* _ASM_MMU_H */
