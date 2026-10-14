/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-01-29     lizhirui     first version
 */

#include <rthw.h>
#include <rtdef.h>
#include <board.h>
#include <cache.h>

/**
 * @brief    Handle an instruction-cache request on the local CPU.
 *
 * @note     The local implementation uses data and instruction barriers. It does
 *           not walk individual cache lines or broadcast to other CPUs.
 *
 * @param    ops is RT_HW_CACHE_INVALIDATE; other operations are ignored.
 *
 * @param    addr is the start of the requested address range.
 *
 * @param    size is the requested length in bytes.
 */
void rt_hw_cpu_icache_ops(int ops, void *addr, int size)
{
    if (ops == RT_HW_CACHE_INVALIDATE)
    {
        rt_hw_cpu_icache_invalidate(addr, size);
    }
}

/**
 * @brief    Order data accesses for a data-cache maintenance request.
 *
 * @note     Both local helpers use a data barrier for coherent cached memory;
 *           they do not implement cache-line maintenance for noncoherent DMA.
 *
 * @param    ops selects clean for RT_HW_CACHE_FLUSH, or invalidate otherwise.
 *
 * @param    addr is the start of the requested address range.
 *
 * @param    size is the requested length in bytes.
 */
void rt_hw_cpu_dcache_ops(int ops, void *addr, int size)
{
    if (ops == RT_HW_CACHE_FLUSH)
    {
        rt_hw_cpu_dcache_clean(addr, size);
    }
    else
    {
        rt_hw_cpu_dcache_invalidate(addr, size);
    }
}

/**
 * @brief    Return the instruction-cache status placeholder for this port.
 *
 * @return   Zero; this function does not query hardware cache state.
 */
rt_base_t rt_hw_cpu_icache_status_local()
{
    return 0;
}

/**
 * @brief    Return the data-cache status placeholder for this port.
 *
 * @return   Zero; this function does not query hardware cache state.
 */
rt_base_t rt_hw_cpu_dcache_status()
{
    return 0;
}

/**
 * @brief    Order prior data writes before local instruction execution.
 *
 * @note     Uses dbar/ibar; the range arguments are not used by the local helper.
 *           Other CPUs must perform their own instruction synchronization.
 *
 * @param    addr is the start of the modified code range.
 *
 * @param    size is the range length in bytes.
 */
void rt_hw_sync_cache_local(void *addr, int size)
{
    rt_hw_cpu_icache_invalidate_local(addr, size);
}
