/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */

#include <rtthread.h>
#include <rthw.h>
#include <string.h>

int main(void)
{
    // rt_kprintf("Hello LoongArch64!\n");
    rt_kprintf("\033[1m\033[32mHello LoongArch64!\033[0m\n");
    return 0;
}
