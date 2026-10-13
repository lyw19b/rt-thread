/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef RT_USING_NANO
#define UART_BASE ((volatile rt_uint8_t *)0x800000001fe001e0UL)

void rt_hw_console_output(const char *str)
{
    while (*str)
    {
        if (*str == '\n')
        {
            while (!(UART_BASE[5] & 0x20)) {}
            UART_BASE[0] = '\r';
        }
        while (!(UART_BASE[5] & 0x20)) {}
        UART_BASE[0] = *str++;
    }
}

char rt_hw_console_getchar(void)
{
    if (UART_BASE[5] & 1)
        return UART_BASE[0];
    rt_thread_mdelay(1);
    return -1;
}
#endif
