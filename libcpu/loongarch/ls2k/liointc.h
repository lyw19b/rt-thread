/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K_LIOINTC_H__
#define LS2K_LIOINTC_H__

#include "ls2k.h"

#define LS2K_LIO_BANK(i)       (LS2K_UNCACHED(LS2K_CONFIG_BASE + 0x1400) + ((i) / 32) * 0x40)
#define LS2K_IRQ_ROUTE_REG(i)  (LS2K_LIO_BANK(i) + ((i) % 32))
#define LS2K_INTISR_REG(i)     (LS2K_LIO_BANK(i) + 0x20)
#define LS2K_INTEN_REG(i)      (LS2K_LIO_BANK(i) + 0x24)
#define LS2K_INTENSET_REG(i)   (LS2K_LIO_BANK(i) + 0x28)
#define LS2K_INTENCLR_REG(i)   (LS2K_LIO_BANK(i) + 0x2c)
#define LS2K_INTPOL_REG(i)     (LS2K_LIO_BANK(i) + 0x30)
#define LS2K_INTEDGE_REG(i)    (LS2K_LIO_BANK(i) + 0x34)
#define LS2K_BOUNCE_REG(i)     (LS2K_LIO_BANK(i) + 0x38)
#define LS2K_INTAUTO_REG(i)    (LS2K_LIO_BANK(i) + 0x3c)
#define LS2K_COREISR_REG0(i)   (LS2K_UNCACHED(LS2K_CONFIG_BASE + 0x1040) + (i) * 0x100)
#define LS2K_COREISR_REG1(i)   (LS2K_COREISR_REG0(i) + 8)

#endif
