/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LS2K_UART_H__COMMON
#define LS2K_UART_H__COMMON
#include <rtthread.h>

struct ls2k_uart_config
{
    rt_ubase_t base;
    rt_uint32_t clock;
    unsigned int irq;
    const char *name;
    rt_uint8_t fifo_control;
};

const struct ls2k_uart_config *ls2k_uart_get_config(rt_size_t *count);
void rt_hw_uart_init(void);

#endif
