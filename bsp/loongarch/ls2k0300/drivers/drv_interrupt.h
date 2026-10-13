/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K0300_INTERRUPT_H__
#define LS2K0300_INTERRUPT_H__

/* Processor User Manual V1.1, section 3.5, traditional 64-source routing. */
#define MAX_INTR                   64
#define LS2K_CONFIG_BASE            0x16000000UL
#define LS2K_LIO_ROUTE_VALUE        0x10 /* INT0; low four bits are reserved */
#define LS2K_LIO_HAS_BOUNCE         0
#include "liointc.h"

void ls2k_interrupt_prepare(void);
void rt_hw_interrupt_init(void);
void liointc_set_irq_mode(int irq, int mode);

#endif
