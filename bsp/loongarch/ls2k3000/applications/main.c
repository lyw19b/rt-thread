/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: main.c
 * Author: LoongsonLab
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 */

/*
 * Description:
 *   Report the kernel and firmware handoff state.
 */

#include <rtthread.h>
#include "board.h"

int main(void)
{
    rt_kprintf("LS2K3000 RT-Thread ready: firmware services exited, CPUs=%u\n",
               RT_CPUS_NR);
    ls2k3000_boot_status();

#ifdef BSP_USING_BOOT_SCREEN
    if (ls2k3000_display_test() != RT_EOK)
    {
        rt_kprintf("DISPLAY: no usable firmware framebuffer for startup screen\n");
    }
#endif

    return 0;
}
