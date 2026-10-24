/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-10-24     OpenLANPU    Bootstrap the LS2K0300 kernel without DMW windows
 */

#include <rtconfig.h>

#ifdef KMD_USE_PAGE
#include "mmu.h"
#include "loongarch.h"

#if KERNEL_VADDR_START != 0xffffffc000000000
#error "PAGE mode requires the LoongArch high-half dynamic address-space layout"
#endif

#if (BSP_RAM_END & 4095) != 0
#error "PAGE mode requires a 4 KiB aligned BSP_RAM_END"
#endif

/* RAM ends below 256 MiB. Device apertures occupy 0x14000000..0x18000000.
 * Reserve two directory pages and enough leaf pages for RAM, devices and the
 * dedicated DMA section. These tables are permanent, never allocator-owned. */
#define BOOT_TABLE_COUNT (((BSP_RAM_END + 0x1fffffUL) >> 21) + 36)

rt_align(ARCH_PAGE_SIZE) static rt_ubase_t boot_tables[BOOT_TABLE_COUNT][512];
static unsigned int boot_table_used;

extern rt_ubase_t MMUTable[512];
rt_align(ARCH_PAGE_SIZE) rt_ubase_t loongarch_empty_pgdl[512];
extern char __text_start[], __text_end[];
extern char __dma_start[], __dma_end[];
extern char __vectors_start[], __vectors_end[];

/**
 * @brief Install a leaf while running at physical addresses before paging.
 * @note BSS has been cleared. No allocator, logging, locks or absolute pointer
 *       initializers may be used on this path. All tables remain below 4 GiB.
 */
static void boot_map_page(rt_ubase_t va, rt_ubase_t pa, rt_ubase_t attr)
{
    rt_ubase_t *table = (void *)LOONGARCH_ALIAS_PHYS(MMUTable);
    unsigned int shift;

    for (shift = 30; shift > ARCH_PAGE_SHIFT; shift -= 9)
    {
        rt_ubase_t *entry = &table[(va >> shift) & 511];

        if (!*entry)
        {
            if (boot_table_used == BOOT_TABLE_COUNT)
            {
                /* A broken static budget must not corrupt the boot image. */
                for (;;)
                {
                    __asm__ volatile("idle 0");
                }
            }

            *entry = (rt_ubase_t)boot_tables[boot_table_used++];
        }

        table = (rt_ubase_t *)*entry;
    }

    table[(va >> ARCH_PAGE_SHIFT) & 511] = pa | attr | MMU_PAGE_PRESENT;
}

void loongarch_boot_map(void)
{
    rt_ubase_t pa;
    rt_ubase_t text_start = LOONGARCH_ALIAS_PHYS(__text_start);
    rt_ubase_t text_end = RT_ALIGN(LOONGARCH_ALIAS_PHYS(__text_end), ARCH_PAGE_SIZE);
    rt_ubase_t dma_start = LOONGARCH_ALIAS_PHYS(__dma_start);
    rt_ubase_t dma_end = LOONGARCH_ALIAS_PHYS(__dma_end);
    rt_ubase_t vectors_start = LOONGARCH_ALIAS_PHYS(__vectors_start);
    rt_ubase_t vectors_end = LOONGARCH_ALIAS_PHYS(__vectors_end);

    for (pa = 0x200000; pa < BSP_RAM_END; pa += ARCH_PAGE_SIZE)
    {
        rt_ubase_t attr = MMU_MAP_K_RWCB | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC;

        if (pa >= dma_start && pa < dma_end)
        {
            /* DMA storage has only an uncached alias, never a cached synonym. */
            boot_map_page(LOONGARCH_UNCACHED_BASE + pa, pa,
                          MMU_MAP_K_DEVICE | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC);
            continue;
        }

        if (pa >= text_start && pa < text_end)
        {
            attr &= ~(MMU_PAGE_WRITE | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC);
        }
        else if (pa >= vectors_start && pa < vectors_end)
        {
            /* trap_init constructs these stubs, then seals them read/execute. */
            attr &= ~MMU_PAGE_NO_EXEC;
        }

        boot_map_page(LOONGARCH_CACHED_BASE + pa, pa, attr);
    }

    for (pa = 0x14000000; pa < 0x18000000; pa += ARCH_PAGE_SIZE)
    {
        boot_map_page(LOONGARCH_UNCACHED_BASE + pa, pa,
                      MMU_MAP_K_DEVICE | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC);
    }

    __asm__ volatile("dbar 0" ::: "memory");
}

/** @brief Seal generated vectors before other CPUs can use them. */
void loongarch_kernel_vectors_protect(void)
{
    rt_ubase_t va;

    for (va = (rt_ubase_t)__vectors_start; va < (rt_ubase_t)__vectors_end;
         va += ARCH_PAGE_SIZE)
    {
        rt_ubase_t *mid = (void *)ARCH_PAGE_PA2VA(MMUTable[(va >> 30) & 511]);
        rt_ubase_t *leaf = (void *)ARCH_PAGE_PA2VA(mid[(va >> 21) & 511]);

        leaf[(va >> 12) & 511] &= ~(MMU_PAGE_WRITE | MMU_PAGE_DIRTY);
    }

    __asm__ volatile("dbar 0\n\tibar 0" ::: "memory");
    rt_hw_tlb_invalidate_all_local();
}
#endif /* KMD_USE_PAGE */
