/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <ioremap.h>
#include <mm_private.h>
#include "mmu.h"
#include "loongarch.h"

rt_align(ARCH_PAGE_SIZE) rt_ubase_t MMUTable[512];
static void *current_mmu_table[RT_CPUS_NR];

static rt_ubase_t *table_from_entry(rt_ubase_t entry)
{
    return (rt_ubase_t *)ARCH_PAGE_VA_MASK(entry & ARCH_PAGE_ADDRESS_MASK);
}

/* Intermediate entries contain physical addresses; leaves also contain flags. */
static rt_ubase_t *walk(rt_aspace_t aspace, rt_ubase_t va, rt_bool_t create)
{
    rt_ubase_t *table = aspace->page_table;
    int shift;

    for (shift = 30; shift > ARCH_PAGE_SHIFT; shift -= 9)
    {
        rt_ubase_t *entry = &table[(va >> shift) & 511];
        if (!*entry)
        {
            rt_ubase_t *next;
            if (!create)
                return RT_NULL;
            next = rt_pages_alloc_ext(0, PAGE_ANY_AVAILABLE);
            if (!next)
                return RT_NULL;
            rt_memset(next, 0, ARCH_PAGE_SIZE);
            __asm__ volatile("dbar 0" ::: "memory");
            *entry = ARCH_PAGE_PA_MASK(next);
        }
        table = table_from_entry(*entry);
    }
    return &table[(va >> ARCH_PAGE_SHIFT) & 511];
}

static rt_bool_t table_empty(rt_ubase_t *table)
{
    int i;
    for (i = 0; i < 512; i++)
        if (table[i])
            return RT_FALSE;
    return RT_TRUE;
}

static size_t unmap_page(rt_aspace_t aspace, rt_ubase_t va)
{
    rt_ubase_t *root = aspace->page_table;
    rt_ubase_t *mid, *leaf;
    unsigned int i = (va >> 30) & 511;
    unsigned int j = (va >> 21) & 511;

    if (!root[i])
        return (1UL << 30) - (va & ((1UL << 30) - 1));
    
    mid = table_from_entry(root[i]);
    
    if (!mid[j])
        return (1UL << 21) - (va & ((1UL << 21) - 1));
    {
        leaf = table_from_entry(mid[j]);
        
        if (!leaf[(va >> 12) & 511])
            return ARCH_PAGE_SIZE;
        
        leaf[(va >> 12) & 511] = 0;
        
        if (table_empty(leaf))
        {
            mid[j] = 0;
            loongarch_tlb_flush_all();
            rt_pages_free(leaf, 0);
        }
    }
    
    if (table_empty(mid))
    {
        root[i] = 0;
        loongarch_tlb_flush_all();
        rt_pages_free(mid, 0);
    }
    
    return ARCH_PAGE_SIZE;
}

void *rt_hw_mmu_map(rt_aspace_t aspace, void *vaddr, void *paddr,
                    size_t size, size_t attr)
{
    rt_ubase_t va = (rt_ubase_t)vaddr;
    rt_ubase_t pa = (rt_ubase_t)paddr;
    size_t done;

    if (!size || ((va | pa | size) & ARCH_PAGE_MASK))
        return RT_NULL;
    MM_PGTBL_LOCK(aspace);
    
    for (done = 0; done < size; done += ARCH_PAGE_SIZE)
    {
        rt_ubase_t *pte = walk(aspace, va + done, RT_TRUE);
        
        if (!pte)
        {
            /* Also release empty tables allocated for the failed page. */
            unmap_page(aspace, va + done);
            while (done)
            {
                done -= ARCH_PAGE_SIZE;
                unmap_page(aspace, va + done);
            }
            loongarch_tlb_flush_all();
            MM_PGTBL_UNLOCK(aspace);
            return RT_NULL;
        }
        
        /* The aspace layer may replace a leaf while expanding a mapping. */
        /* PTW tests Present (bit 7), while software refill uses Valid (bit 0).
         * Keep both so the same page tables work with either refill path. */
        *pte = (pa + done) | attr | MMU_PAGE_PRESENT;
        if (attr & MMU_PAGE_WRITE)
            *pte |= MMU_PAGE_DIRTY;
    }
    
    __asm__ volatile("dbar 0" ::: "memory");
    loongarch_tlb_flush_all();
    MM_PGTBL_UNLOCK(aspace);
    return vaddr;
}

void rt_hw_mmu_unmap(rt_aspace_t aspace, void *vaddr, size_t size)
{
    size_t offset;
    MM_PGTBL_LOCK(aspace);
    
    /* Large user reservations are sparse: skip absent page-table subtrees. */
    for (offset = 0; offset < size;)
        offset += unmap_page(aspace, (rt_ubase_t)vaddr + offset);
    
    loongarch_tlb_flush_all();
    MM_PGTBL_UNLOCK(aspace);
}

void *rt_hw_mmu_v2p(rt_aspace_t aspace, void *vaddr)
{
    rt_ubase_t va = (rt_ubase_t)vaddr;
    rt_ubase_t *pte;

    if (aspace == &rt_kernel_space && (va >> 60 == 9 || va >> 60 == 8))
        return (void *)(va & 0x0000ffffffffffffUL);
    
    pte = walk(aspace, va, RT_FALSE);
    
    if (!pte || !(*pte & MMU_PAGE_VALID))
        return ARCH_MAP_FAILED;
    
    return (void *)((*pte & ARCH_PAGE_ADDRESS_MASK) | (va & ARCH_PAGE_MASK));
}

void rt_hw_aspace_switch(rt_aspace_t aspace)
{
    rt_ubase_t pa = ARCH_PAGE_PA_MASK(aspace->page_table);
    current_mmu_table[rt_hw_cpu_id()] = aspace->page_table;
    csr_write64(pa, LOONGARCH_CSR_PGDL);
    rt_hw_tlb_invalidate_all_local();
}

void *rt_hw_mmu_tbl_get(void)
{
    return current_mmu_table[rt_hw_cpu_id()];
}

int rt_hw_mmu_map_init(rt_aspace_t aspace, void *vaddr, rt_ubase_t size,
                       rt_ubase_t *table, rt_ubase_t pv_off)
{
    rt_aspace_init(aspace, (void *)KERNEL_VADDR_START, KERNEL_VADDR_SIZE, table);
    rt_ioremap_start = vaddr;
    rt_ioremap_size = size;
    rt_mpr_start = (char *)vaddr - rt_mpr_size;
    return 0;
}

void rt_hw_mmu_setup(rt_aspace_t aspace, struct mem_desc *descs, int count)
{
    /* The kernel image uses DMW1; PGDH serves MPR and ioremap addresses. */
    rt_ubase_t pa = ARCH_PAGE_PA_MASK(aspace->page_table);
    csr_write64(pa, LOONGARCH_CSR_PGDH);
    rt_hw_aspace_switch(aspace);
    rt_page_cleanup();
}

void *rt_hw_mmu_pgtbl_create(void)
{
    void *table = rt_pages_alloc_ext(0, PAGE_ANY_AVAILABLE);
    
    if (table)
        rt_memset(table, 0, ARCH_PAGE_SIZE);
    
    return table;
}

void rt_hw_mmu_pgtbl_delete(void *table)
{
    rt_pages_free(table, 0);
}

int rt_hw_mmu_control(rt_aspace_t aspace, void *vaddr, size_t size,
                      enum rt_mmu_cntl cmd)
{
    size_t offset;
    if (cmd != MMU_CNTL_CACHE && cmd != MMU_CNTL_NONCACHE)
        return -RT_ENOSYS;
    
    MM_PGTBL_LOCK(aspace);
    
    for (offset = 0; offset < size; offset += ARCH_PAGE_SIZE)
    {
        rt_ubase_t *pte = walk(aspace, (rt_ubase_t)vaddr + offset, RT_FALSE);
        if (pte && *pte)
        {
            *pte &= ~(3UL << MMU_CACHE_SHIFT);
            if (cmd == MMU_CNTL_CACHE)
                *pte |= MMU_CACHE_CC;
        }
    }
    
    loongarch_tlb_flush_all();
    MM_PGTBL_UNLOCK(aspace);
    return RT_EOK;
}
