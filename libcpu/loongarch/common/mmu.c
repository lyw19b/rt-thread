/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <ioremap.h>
#include <mm_private.h>
#include <lwp.h>
#include <lwp_arch.h>

#include "mmu.h"
#include "loongarch.h"
#include "inst.h"

rt_align(ARCH_PAGE_SIZE) rt_ubase_t MMUTable[512];

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
 * @brief    Reclaim empty tables, optionally removing a leaf mapping first.
 *
 * @note     Remote TLB invalidation completes before a page-table page is freed.
 *           The mapped data page remains owned by the address-space layer.
 *
 * @param    aspace is the address space whose page-table lock is held.
 *
 * @param    va is the page-aligned virtual address to unmap.
 *
 * @param    unmap selects leaf removal; false preserves all existing mappings.
 *
 * @return   Bytes to skip before the next leaf or subtree requiring inspection.
 */
static size_t release_page(rt_aspace_t aspace, rt_ubase_t va, rt_bool_t unmap)
{
    rt_ubase_t *root = aspace->page_table;
    rt_ubase_t *mid, *leaf;
    unsigned int i = (va >> 30) & 511;
    unsigned int j = (va >> 21) & 511;
    size_t span = (1UL << 21) - (va & ((1UL << 21) - 1));

    if (!root[i])
        return (1UL << 30) - (va & ((1UL << 30) - 1));
    
    mid = table_from_entry(root[i]);
    
    if (mid[j])
    {
        leaf = table_from_entry(mid[j]);

        if (unmap)
        {
            leaf[(va >> 12) & 511] = 0;
            span = ARCH_PAGE_SIZE;
        }
        
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
    
    return span;
}

/**
 * @brief    Map a contiguous physical range into an address space.
 *
 * @note     Takes the page-table lock and completes TLB invalidation on all online
 *           CPUs before returning. Existing leaf entries in the range may be replaced.
 *           Allocation failure leaves existing mappings intact.
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
    
    /* Reserve all tables before replacing any leaf. Each leaf table covers 2 MiB. */
    for (done = 0; done < size;)
    {
        if (!walk(aspace, va + done, RT_TRUE))
        {
            size_t offset;

            /* Include the partially allocated path of the failed walk. */
            for (offset = 0; offset <= done;)
                offset += release_page(aspace, va + offset, RT_FALSE);
            MM_PGTBL_UNLOCK(aspace);
            return RT_NULL;
        }
        done += (1UL << 21) - ((va + done) & ((1UL << 21) - 1));
    }

    for (done = 0; done < size; done += ARCH_PAGE_SIZE)
    {
        rt_ubase_t *pte = walk(aspace, va + done, RT_FALSE);

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
        offset += release_page(aspace, (rt_ubase_t)vaddr + offset, RT_TRUE);
    
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

#ifndef KMD_USE_PAGE
    if (aspace == &rt_kernel_space && (va >> 60 == 9 || va >> 60 == 8))
        return (void *)(va & 0x0000ffffffffffffUL);
#else
    /* Never interpret an obsolete DMW alias as a canonical paged address. */
    if ((va >> 38) != 0 && (va >> 38) != ((1UL << 26) - 1))
        return ARCH_MAP_FAILED;
#endif
    
    pte = walk(aspace, va, RT_FALSE);
    
    if (!pte || !(*pte & MMU_PAGE_VALID))
        return ARCH_MAP_FAILED;
    
    return (void *)((*pte & ARCH_PAGE_ADDRESS_MASK) | (va & ARCH_PAGE_MASK));
}

static rt_bool_t ale_user_permission(rt_ubase_t attr, rt_bool_t write,
                                     rt_bool_t instruction)
{
    if ((attr & MMU_PAGE_USER) != MMU_PAGE_USER ||
        (attr & (1UL << MMU_PAGE_RPLV_SHIFT)))
    {
        return RT_FALSE;
    }

    if (instruction)
    {
        return !(attr & MMU_PAGE_NO_EXEC);
    }

    if (write)
    {
        return (attr & MMU_PAGE_WRITE) != 0;
    }

    return (attr & MMU_PAGE_VALID) && !(attr & (1UL << MMU_PAGE_NO_READ_SHIFT));
}

/**
 * @brief Copy up to eight user bytes for scalar ALE recovery, without faulting.
 * @note Called with scheduling available and IRQs enabled. The address-space
 *       mutex pins both mappings until the complete copy finishes. Both pages
 *       are checked before stores; demand paging and CoW use the normal VM
 *       resolver. MMIO and noncached mappings are never accessed bytewise.
 */
rt_bool_t loongarch_unaligned_user_copy(void *buffer, rt_ubase_t address,
                                      rt_size_t size, rt_bool_t write,
                                      rt_bool_t instruction)
{
    struct rt_lwp *lwp = lwp_self();
    rt_aspace_t aspace;
    rt_uint8_t *aliases[2];
    rt_size_t lengths[2];
    rt_size_t count = 0, remaining = size, i, j;
    rt_ubase_t cursor = address;
    rt_bool_t success = RT_FALSE;

    if (!lwp || !lwp->aspace || !size || size > 8 ||
        address < USER_VADDR_START || address >= USER_VADDR_TOP ||
        size > USER_VADDR_TOP - address)
    {
        return RT_FALSE;
    }

    aspace = lwp->aspace;

    if (rt_mutex_take(&aspace->bst_lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        return RT_FALSE;
    }

    while (remaining)
    {
        rt_varea_t varea = rt_aspace_query(aspace, (void *)cursor);
        rt_ubase_t *pte;
        rt_ubase_t entry, alias;
        rt_size_t chunk = ARCH_PAGE_SIZE - (cursor & ARCH_PAGE_MASK);
        struct rt_aspace_fault_msg fault = {
            .fault_vaddr = (void *)cursor,
            .fault_type = MM_FAULT_TYPE_GENERIC_MMU,
            .fault_op = instruction ? MM_FAULT_OP_EXECUTE :
                        write ? MM_FAULT_OP_WRITE : MM_FAULT_OP_READ,
        };

        if (chunk > remaining)
        {
            chunk = remaining;
        }

        if (!varea ||
            !ale_user_permission(varea->attr, write, instruction) ||
            (varea->attr & (3UL << MMU_CACHE_SHIFT)) != MMU_CACHE_CC)
        {
            goto out;
        }

        pte = walk(aspace, cursor, RT_FALSE);

        if (!pte || !(*pte & MMU_PAGE_VALID) ||
            (write && ((*pte & (MMU_PAGE_WRITE | MMU_PAGE_DIRTY)) !=
                       (MMU_PAGE_WRITE | MMU_PAGE_DIRTY))))
        {
            /* bst_lock is recursive; the resolver owns the normal CoW path. */
            if (!rt_aspace_fault_try_fix(aspace, &fault))
            {
                goto out;
            }

            pte = walk(aspace, cursor, RT_FALSE);
        }

        if (!pte)
        {
            goto out;
        }

        entry = *pte;

        if (!(entry & MMU_PAGE_VALID) ||
            !ale_user_permission(entry, write, instruction) ||
            (entry & (3UL << MMU_CACHE_SHIFT)) != MMU_CACHE_CC ||
            (write && !(entry & MMU_PAGE_DIRTY)))
        {
            goto out;
        }

        alias = ARCH_PAGE_PA2VA((entry & ARCH_PAGE_ADDRESS_MASK) |
                                (cursor & ARCH_PAGE_MASK));

        if (!loongarch_unaligned_access_ok(alias, chunk, write, RT_FALSE))
        {
            goto out;
        }

        aliases[count] = (rt_uint8_t *)alias;
        lengths[count++] = chunk;
        cursor += chunk;
        remaining -= chunk;
    }

    for (i = 0; i < count; i++)
    {
        for (j = 0; j < lengths[i]; j++)
        {
            if (write)
            {
                ((volatile rt_uint8_t *)aliases[i])[j] = *(rt_uint8_t *)buffer;
            }
            else
            {
                *(rt_uint8_t *)buffer = ((volatile rt_uint8_t *)aliases[i])[j];
            }

            buffer = (rt_uint8_t *)buffer + 1;
        }
    }

    success = RT_TRUE;

out:
    rt_mutex_release(&aspace->bst_lock);

    return success;
}

/**
 * @brief    Install the local CPU user page-table root and discard its TLB entries.
 *
 * @note     Masks local IRQs until the root and TLB agree, preventing migration
 *           even when called from fork or exec. Preserves the caller's IRQ state.
 *           PGDH remains unchanged; the kernel mapping is shared across address spaces.
 *
 * @param    aspace is the address space to activate.
 */
void rt_hw_aspace_switch(rt_aspace_t aspace)
{
    rt_base_t level = rt_hw_local_irq_disable();
    rt_ubase_t pa = ARCH_PAGE_PA_MASK(aspace->page_table);

#ifdef KMD_USE_PAGE
    if (aspace == &rt_kernel_space)
    {
        extern rt_ubase_t loongarch_empty_pgdl[];

        pa = ARCH_PAGE_PA_MASK(loongarch_empty_pgdl);
    }
#endif

    csr_write64(pa, LOONGARCH_CSR_PGDL);
    rt_hw_tlb_invalidate_all_local();
    rt_hw_local_irq_enable(level);
}

/**
 * @brief    Read the user page-table root installed in the local CPU.
 *
 * @return   The root table virtual address, or RT_NULL when PGDL is zero.
 */
void *rt_hw_mmu_tbl_get(void)
{
    rt_ubase_t pa = csr_read64(LOONGARCH_CSR_PGDL);

    return pa ? table_from_entry(pa) : RT_NULL;
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
 * @param    pv_off is the generic physical-to-virtual offset, set by the BSP.
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
 * @note     PAGE mode reuses the populated bootstrap root; DMW mode starts with
 *           an empty root for dynamic mappings. Both finish early page allocation.
 *
 * @param    aspace is the initialized kernel address space.
 *
 * @param    descs is the generic memory-descriptor argument, unused by this port.
 *
 * @param    count is the generic descriptor count, unused by this port.
 */
void rt_hw_mmu_setup(rt_aspace_t aspace, struct mem_desc *descs, int count)
{
    /* PAGE's root already contains permanent RAM/device mappings. Do not clear it. */
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
