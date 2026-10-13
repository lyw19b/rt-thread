/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ls2k.h"
#include "drv_pci.h"
#include "drv_interrupt.h"

rt_err_t ls2k_gmac_platform_init(unsigned int port, rt_ubase_t *base, int *irq)
{
    if (port > 1 || !base || !irq)
        return -RT_EINVAL;
    *irq = port ? LS2K_GMAC1_SBD_IRQ : LS2K_GMAC0_SBD_IRQ;
    return ls2k_pci_enable_gmac(port, base);
}

rt_ubase_t ls2k_gmac_dma_limit(void)
{
    return 1UL << 32;
}
