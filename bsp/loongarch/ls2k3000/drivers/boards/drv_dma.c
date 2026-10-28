/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Permanently pin DMA buffers outside the heap and Smart page allocator.
 *   Use the cached alias for the Explorer's coherent DMA fabric. Mixing CPU
 *   uncached accesses with DMA accesses through shared cache loses ownership
 *   updates after a descriptor cache line has been fetched by the device.
 */

#include <rthw.h>
#include "board.h"
#include "loongarch.h"

#ifdef BSP_USING_DMA_POOL
static struct rt_spinlock pool_lock;
static rt_size_t allocated;
static int pool_ready;
static uint32_t coherent_config;

int ls2k3000_dma_init(void)
{
    unsigned long layout = read_cpucfg(0x10);
    unsigned long l1_line = (read_cpucfg(0x12) >> 24) & 0x7f;
    unsigned long l2_line = (read_cpucfg(0x13) >> 24) & 0x7f;

    /* LA364E: separate L1 I/D (leaves 0/1), unified L2 (leaf 2). */
    if ((layout & 0x1f) != 0x1d || l1_line < 4 || l1_line > 8 ||
        l2_line < 4 || l2_line > 8 || rt_hw_cpu_id() != 0)
    {
        return -RT_ENOSYS;
    }

    /* The physical Explorer exposes this chip register; synthetic QEMU
     * handoffs have no such resource. Never change the firmware DMA policy. */
    if (ls2k3000_boot->reset_address == UINT64_C(0x10150030))
    {
        coherent_config = HWREG32(LOONGARCH_PHYS_UNCACHED(0x1fe00470));

        /* Match the Explorer firmware policy observed with its working
         * coherent Linux drivers. Other policies need explicit validation. */
        if (coherent_config != 0)
        {
            return -RT_ENOSYS;
        }
    }

    for (rt_ubase_t offset = 0; offset < LS2K3000_DMA_BYTES; offset += 1UL << l1_line)
    {
        rt_ubase_t address = LOONGARCH_PHYS_CACHED(LS2K3000_DMA_PHYS + offset);
        __asm__ volatile("cacop 0x11, %0, 0" :: "r"(address) : "memory");
    }

    rt_hw_dmb();

    for (rt_ubase_t offset = 0; offset < LS2K3000_DMA_BYTES; offset += 1UL << l2_line)
    {
        rt_ubase_t address = LOONGARCH_PHYS_CACHED(LS2K3000_DMA_PHYS + offset);
        __asm__ volatile("cacop 0x12, %0, 0" :: "r"(address) : "memory");
    }

    rt_hw_dmb();
    rt_spin_lock_init(&pool_lock);
    pool_ready = 1;
    return RT_EOK;
}

void *ls2k3000_dma_alloc(rt_size_t bytes, rt_size_t alignment, uint64_t *physical)
{
    rt_base_t level;
    rt_size_t offset;
    void *address;

    if (!pool_ready || !physical || !bytes || !alignment ||
        alignment > LS2K3000_DMA_BYTES || (alignment & (alignment - 1)))
    {
        return RT_NULL;
    }

    level = rt_spin_lock_irqsave(&pool_lock);
    offset = RT_ALIGN(allocated, alignment);

    if (offset > LS2K3000_DMA_BYTES || bytes > LS2K3000_DMA_BYTES - offset)
    {
        rt_spin_unlock_irqrestore(&pool_lock, level);
        return RT_NULL;
    }

    allocated = offset + bytes;
    rt_spin_unlock_irqrestore(&pool_lock, level);
    *physical = LS2K3000_DMA_PHYS + offset;
    address = (void *)LOONGARCH_PHYS_CACHED(*physical);
    rt_memset(address, 0, bytes);
    rt_hw_dmb();
    return address;
}

static void ls2k3000_dma_status(void)
{
    rt_kprintf("DMA: coherent cached pool=%lx used=%lu capacity=%lu config=%08x ready=%d\n",
               (unsigned long)LS2K3000_DMA_PHYS, (unsigned long)allocated,
               (unsigned long)LS2K3000_DMA_BYTES, coherent_config, pool_ready);
}

MSH_CMD_EXPORT(ls2k3000_dma_status, Show dedicated coherent DMA pool);
#endif
