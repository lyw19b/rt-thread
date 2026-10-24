/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-10-24     OpenLANPU    Define independent kernel RAM and device windows
 */

#ifndef LOONGARCH_KERNEL_MAP_H__
#define LOONGARCH_KERNEL_MAP_H__

#include <rtconfig.h>

#ifdef KMD_USE_PAGE
#define LOONGARCH_CACHED_BASE    0xffffffe000000000
#define LOONGARCH_UNCACHED_BASE  0xffffffe100000000
#define LOONGARCH_PHYSICAL_MASK 0x00000000ffffffff
#else
#define LOONGARCH_CACHED_BASE    0x9000000000000000
#define LOONGARCH_UNCACHED_BASE  0x8000000000000000
#define LOONGARCH_PHYSICAL_MASK 0x0000ffffffffffff
#endif

/* Only fixed RAM/device aliases and physical addresses belong to this API.
 * Dynamic ioremap and user addresses must be translated through their tables. */
#ifndef __ASSEMBLY__
#define LOONGARCH_ALIAS_PHYS(x) ((unsigned long)(x) & LOONGARCH_PHYSICAL_MASK)
#define LOONGARCH_PHYS_CACHED(x) (LOONGARCH_ALIAS_PHYS(x) | LOONGARCH_CACHED_BASE)
#define LOONGARCH_PHYS_UNCACHED(x) (LOONGARCH_ALIAS_PHYS(x) | LOONGARCH_UNCACHED_BASE)

#ifdef KMD_USE_PAGE
void loongarch_boot_map(void);
void loongarch_kernel_vectors_protect(void);
#endif
#endif

#endif /* LOONGARCH_KERNEL_MAP_H__ */
