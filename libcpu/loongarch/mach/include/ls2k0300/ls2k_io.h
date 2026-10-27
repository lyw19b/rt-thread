/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               integrate reference LS2K0300 peripherals
 */

/*
 * Description:
 *   Loongson platform definitions and initialization.
 */

#ifndef LS2K0300_REFERENCE_IO_H
#define LS2K0300_REFERENCE_IO_H

#include <rtthread.h>
#include <rthw.h>
#include "kernel_map.h"
#include "loongarch.h"

#define PHYS_TO_UNCACHED(address) LOONGARCH_PHYS_UNCACHED(address)
#define PHYS_TO_CACHED(address)   LOONGARCH_PHYS_CACHED(address)
#define UNCACHED_TO_PHYS(address) LOONGARCH_ALIAS_PHYS(address)
#define CACHED_TO_PHYS(address)   LOONGARCH_ALIAS_PHYS(address)
#define MHZ_TO_HZ                 1000000UL
#define SECOND_TO_NS              1000000000UL
#ifndef BIT
#define BIT(bit) (1UL << (bit))
#endif

/* MMIO accesses must not be reordered across normal memory or each other. */
#define LS2K_IO_ACCESS(width, type)                                      \
    static inline type ls2k_read##width(rt_ubase_t address)              \
    {                                                                    \
        type value;                                                      \
        rt_hw_dmb();                                                     \
        value = *(volatile type *)address;                               \
        rt_hw_dmb();                                                     \
        return value;                                                    \
    }                                                                    \
    static inline void ls2k_write##width(type value, rt_ubase_t address) \
    {                                                                    \
        rt_hw_dmb();                                                     \
        *(volatile type *)address = value;                               \
        rt_hw_dmb();                                                     \
    }

LS2K_IO_ACCESS(8, rt_uint8_t)
LS2K_IO_ACCESS(16, rt_uint16_t)
LS2K_IO_ACCESS(32, rt_uint32_t)
LS2K_IO_ACCESS(64, rt_uint64_t)
#undef LS2K_IO_ACCESS

#define readb(address)         ls2k_read8((rt_ubase_t)(address))
#define readw(address)         ls2k_read16((rt_ubase_t)(address))
#define readl(address)         ls2k_read32((rt_ubase_t)(address))
#define readq(address)         ls2k_read64((rt_ubase_t)(address))
#define writeb(value, address) ls2k_write8((value), (rt_ubase_t)(address))
#define writew(value, address) ls2k_write16((value), (rt_ubase_t)(address))
#define writel(value, address) ls2k_write32((value), (rt_ubase_t)(address))
#define writeq(value, address) ls2k_write64((value), (rt_ubase_t)(address))

#endif
