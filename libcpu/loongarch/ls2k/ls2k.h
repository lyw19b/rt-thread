/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K_H__
#define LS2K_H__

#include <rtthread.h>
#include "kernel_map.h"

#define LS2K_PHYSICAL(address) LOONGARCH_ALIAS_PHYS(address)
#define LS2K_UNCACHED(address) LOONGARCH_PHYS_UNCACHED(address)

#endif
