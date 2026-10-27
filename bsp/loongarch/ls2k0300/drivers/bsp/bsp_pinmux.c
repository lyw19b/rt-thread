/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: bsp pinmux.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include "bsp_pinmux.h"
#include "loongson_soc_info.h"

int pinmux_pin_setup(int pin, int mode)
{
    volatile unsigned int *cfg_base_reg;
    int group;
    int group_num;
    unsigned int temp;
    rt_base_t level;
    if (pin < 0 || pin > LS_GPIO_MAX || mode < 0 || mode > 3)
    {
        rt_kprintf("Error %s pin not in [0-105] (%d)\r\n", __func__, pin);
        return -1;
    }

    group = (pin >> 4);
    group_num = (pin & 0xf);
    group_num <<= 1; // * 2 就是设置pin的初始bit

    cfg_base_reg = (unsigned int *)LS_GPIO_0_15_MULTI_CFG;
    cfg_base_reg += group;
    level = rt_hw_interrupt_disable();
    temp = readl(cfg_base_reg);
    temp |= (0x3U << group_num);     // 清 0
    temp ^= (0x3U << group_num);     // 清 0
    temp |= ((unsigned int)mode << group_num); // 设置对应的模式
    writel(temp, cfg_base_reg);
    rt_hw_interrupt_enable(level);

    return 0;
}
