/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K_H__
#define LS2K_H__

#include <rtthread.h>

#define LS2K_PHYSICAL(address) ((rt_ubase_t)(address) & 0x0000ffffffffffffUL)
#define LS2K_UNCACHED(address) (LS2K_PHYSICAL(address) | 0x8000000000000000UL)

#endif
