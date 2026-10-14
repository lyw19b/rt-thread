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

/**
 * @brief    Locate a leaf entry in the three-level page table.
 *
 * @note     The caller must serialize table allocation and reclamation. Intermediate
 *           entries contain physical addresses; leaf entries also contain attributes.
 *
 * @param    aspace is the address space to walk.
 *
 * @param    va is the virtual address to look up.
 *
 * @param    create is RT_TRUE to allocate missing intermediate tables.
 *
 * @return   The leaf entry, or RT_NULL if a table is absent or allocation fails.
 */
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

/**
 * @brief    Remove a leaf mapping and reclaim empty intermediate tables.
 *
 * @note     Remote TLB invalidation completes before a page-table page is freed.
 *           The mapped data page remains owned by the address-space layer.
 *
 * @param    aspace is the address space whose page-table lock is held.
 *
 * @param    va is the page-aligned virtual address to unmap.
 *
 * @return   The number of bytes to skip: one page, or the rest of an absent subtree.
 */
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

/**
 * @brief    Map a contiguous physical range into an address space.
 *
 * @note     Takes the page-table lock and completes TLB invalidation on all online
 *           CPUs before returning. Existing leaf entries in the range may be replaced.
 *
 * @param    aspace is the address space to update.
 *
 * @param    vaddr is the page-aligned virtual start address.
 *
 * @param    paddr is the page-aligned physical start address.
 *
 * @param    size is the nonzero, page-aligned length in bytes.
 *
 * @param    attr is the LoongArch leaf-entry attribute mask.
 *
 * @return   vaddr on success, or RT_NULL for invalid alignment or allocation failure.
 */
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

/**
 * @brief    Remove mappings without freeing their backing data pages.
 *
 * @note     Takes the page-table lock. Missing subtrees are skipped, and remote
 *           TLB invalidation completes before reclaimed tables can be reused.
 *
 * @param    aspace is the address space to update.
 *
 * @param    vaddr is the page-aligned virtual start address.
 *
 * @param    size is the page-aligned length in bytes.
 */
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

/**
 * @brief    Translate a virtual address, including its offset within a page.
 *
 * @note     Kernel DMW0/DMW1 addresses bypass the page-table walk. The caller must
 *           keep the address space and its page tables stable during the lookup.
 *
 * @param    aspace is the address space to inspect.
 *
 * @param    vaddr is the virtual address to translate.
 *
 * @return   The physical address, or ARCH_MAP_FAILED if no valid leaf exists.
 */
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

/**
 * @brief    Install the local CPU user page-table root and discard its TLB entries.
 *
 * @note     The caller must prevent CPU migration during the switch. PGDH remains
 *           unchanged; the kernel mapping is shared across address spaces.
 *
 * @param    aspace is the address space to activate.
 */
void rt_hw_aspace_switch(rt_aspace_t aspace)
{
    rt_ubase_t pa = ARCH_PAGE_PA_MASK(aspace->page_table);
    current_mmu_table[rt_hw_cpu_id()] = aspace->page_table;
    csr_write64(pa, LOONGARCH_CSR_PGDL);
    rt_hw_tlb_invalidate_all_local();
}

/**
 * @brief    Get the page-table root last installed on the local CPU.
 *
 * @return   The root table virtual address, or RT_NULL before the first switch.
 */
void *rt_hw_mmu_tbl_get(void)
{
    return current_mmu_table[rt_hw_cpu_id()];
}

/**
 * @brief    Initialize the kernel address space and reserve its I/O mapping window.
 *
 * @note     Called during boot before mappings or secondary CPUs use the table.
 *
 * @param    aspace is the kernel address-space object to initialize.
 *
 * @param    vaddr is the virtual start address of the ioremap window.
 *
 * @param    size is the ioremap window size in bytes.
 *
 * @param    table is the preallocated root page table.
 *
 * @param    pv_off is the generic physical-to-virtual offset, unused by this DMW port.
 *
 * @return   Zero after initialization.
 */
int rt_hw_mmu_map_init(rt_aspace_t aspace, void *vaddr, rt_ubase_t size,
                       rt_ubase_t *table, rt_ubase_t pv_off)
{
    rt_aspace_init(aspace, (void *)KERNEL_VADDR_START, KERNEL_VADDR_SIZE, table);
    rt_ioremap_start = vaddr;
    rt_ioremap_size = size;
    rt_mpr_start = (char *)vaddr - rt_mpr_size;
    return 0;
}

/**
 * @brief    Activate the kernel page-table roots on the boot CPU.
 *
 * @note     The kernel image uses DMW1. This installs PGDH for mapped kernel
 *           addresses, installs PGDL, and switches the page allocator out of early mode.
 *
 * @param    aspace is the initialized kernel address space.
 *
 * @param    descs is the generic memory-descriptor argument, unused by this port.
 *
 * @param    count is the generic descriptor count, unused by this port.
 */
void rt_hw_mmu_setup(rt_aspace_t aspace, struct mem_desc *descs, int count)
{
    /* The kernel image uses DMW1; PGDH serves MPR and ioremap addresses. */
    rt_ubase_t pa = ARCH_PAGE_PA_MASK(aspace->page_table);
    csr_write64(pa, LOONGARCH_CSR_PGDH);
    rt_hw_aspace_switch(aspace);
    rt_page_cleanup();
}

/**
 * @brief    Allocate and clear a root page-table page.
 *
 * @return   The page-aligned virtual address, or RT_NULL if allocation fails.
 */
void *rt_hw_mmu_pgtbl_create(void)
{
    void *table = rt_pages_alloc_ext(0, PAGE_ANY_AVAILABLE);
    
    if (table)
        rt_memset(table, 0, ARCH_PAGE_SIZE);
    
    return table;
}

/**
 * @brief    Free a root page-table page.
 *
 * @note     All mappings must already be removed, and no CPU may still use this
 *           root. Intermediate tables are reclaimed while removing the mappings.
 *
 * @param    table is the root table allocated by rt_hw_mmu_pgtbl_create().
 */
void rt_hw_mmu_pgtbl_delete(void *table)
{
    rt_pages_free(table, 0);
}

/**
 * @brief    Change the cache attribute of existing leaf mappings.
 *
 * @note     Unmapped pages are skipped. Takes the page-table lock and completes
 *           TLB invalidation on all online CPUs before returning.
 *
 * @param    aspace is the address space to update.
 *
 * @param    vaddr is the page-aligned virtual start address.
 *
 * @param    size is the page-aligned length in bytes.
 *
 * @param    cmd is MMU_CNTL_CACHE or MMU_CNTL_NONCACHE.
 *
 * @return   RT_EOK on completion, or -RT_ENOSYS for an unsupported command.
 */
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
