/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LS2K_GMAC_PLATFORM_H__
#define __LS2K_GMAC_PLATFORM_H__

#include <rtthread.h>

/* Firmware must initialize DDR, pin mux, PHY reset and RGMII delays.
 * The BSP supplies bus resources; the common driver owns MAC/DMA operation. */
rt_err_t ls2k_gmac_platform_init(unsigned int port, rt_ubase_t *base, int *irq);
rt_ubase_t ls2k_gmac_dma_limit(void);

#endif
