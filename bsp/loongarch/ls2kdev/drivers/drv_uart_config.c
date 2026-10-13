/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ls2k_uart.h"
#include "ls2k1000la.h"
#include "drv_interrupt.h"

const struct ls2k_uart_config *ls2k_uart_get_config(rt_size_t *count)
{
    static const struct ls2k_uart_config ports[] =
    {
#ifdef RT_USING_UART0
        {LS_UART0_REG_BASE, 125000000, LS2K_UART_0123_IRQ, "uart0", 0x07},
#endif
#ifdef RT_USING_UART4
        {LS_UART4_REG_BASE, 125000000, LS2K_UART_4567_IRQ, "uart4", 0x07},
#endif
        {0, 0, 0, RT_NULL, 0}
    };

    *count = sizeof(ports) / sizeof(ports[0]) - 1;
    return ports;
}
