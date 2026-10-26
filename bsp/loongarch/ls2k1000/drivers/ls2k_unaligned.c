/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    bound LS2K1000LA ALE recovery to kernel RAM
 */

#include <rtthread.h>
#include <inst.h>
#include "board.h"

extern char __text_start[], __text_end[];
extern char __dma_start[], __dma_end[];
extern char __vectors_start[], __vectors_end[];

/**
 * @brief Allow scalar ALE recovery within the standard kernel's cached RAM.
 * @note Only the linked image and configured heap are accepted. Device, DMA,
 *       uncached and firmware mappings are excluded; code and vectors cannot
 *       be written through the emulator. Check the complete range first.
 */
rt_bool_t loongarch_unaligned_access_ok(rt_ubase_t address, rt_size_t size,
                                      rt_bool_t write, rt_bool_t instruction)
{
    rt_ubase_t end;
#ifdef RT_USING_SMART
    rt_ubase_t ram_end = (rt_ubase_t)RT_HW_PAGE_END;
#else
    rt_ubase_t ram_end = (rt_ubase_t)RT_HW_HEAP_END;
#endif

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
