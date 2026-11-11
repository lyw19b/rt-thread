/* SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2026 OpenLANPU Contributors
 * Description: Explicit revision-B FPGA resources, not inferred from CPU identity.
 */
#include <rtthread.h>
#ifdef BSP_USING_NV2K0302
#include "ls2k.h"
#include "nv2k_rtos.h"
#include <uapi/nv2k0302.h>

static int nv2k_board_init(void)
{
    const struct nv2k_rtos_resources resources = {
        .spi2 = (void *)LS2K_UNCACHED(0x1610c000UL),
        .localio = (void *)LS2K_UNCACHED(0x14000000UL),
        .chipcfg = (void *)LS2K_UNCACHED(0x16000000UL),
#ifdef BSP_NV2K_NV2
        .architecture = NV2K_ARCH_NV2,
#else
        .architecture = NV2K_ARCH_NV1,
#endif
    };
    return nv2k_rtos_register(&resources);
}
INIT_DEVICE_EXPORT(nv2k_board_init);
#endif
