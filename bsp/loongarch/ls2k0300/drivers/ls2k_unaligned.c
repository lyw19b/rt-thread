/*
 * Copyright (c) 2026, OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    bound kernel ALE recovery to stable RAM
 */

#include <rtthread.h>
#include <inst.h>
#include <kernel_map.h>

extern char __text_start[], __text_end[];
extern char __dma_start[], __dma_end[];
extern char __vectors_start[], __vectors_end[];

/**
 * @brief Approve only the BSP's permanent cached RAM mapping for ALE recovery.
 * @note The complete range is checked before any store, including across pages.
 *       DMA aliases, device apertures and dynamic/user mappings are excluded.
 *       PAGE bootstrap maps this entire interval except the DMA section and
 *       makes text/vector pages read-only. Those exclusions also apply in DMW.
 */
rt_bool_t loongarch_unaligned_access_ok(rt_ubase_t address, rt_size_t size,
                                      rt_bool_t write, rt_bool_t instruction)
{
    rt_ubase_t end;
    rt_ubase_t ram_end = LOONGARCH_CACHED_BASE + BSP_RAM_END;

    if (!size || address < (rt_ubase_t)__text_start || address >= ram_end ||
        size > ram_end - address)
    {
        return RT_FALSE;
    }

    end = address + size;

    if (instruction)
    {
        return !write && end <= (rt_ubase_t)__text_end;
    }

    if (address < (rt_ubase_t)__dma_end && end > (rt_ubase_t)__dma_start)
    {
        return RT_FALSE;
    }

    if (write &&
        (address < RT_ALIGN((rt_ubase_t)__text_end, 4096) ||
         (address < (rt_ubase_t)__vectors_end && end > (rt_ubase_t)__vectors_start)))
    {
        return RT_FALSE;
    }

    return RT_TRUE;
}
