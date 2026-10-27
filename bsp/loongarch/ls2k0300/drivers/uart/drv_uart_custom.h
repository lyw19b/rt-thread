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
 *   LS2K0300 development BSP: drv uart custom.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_UART_CUSTOM_H__
#define __DRV_UART_CUSTOM_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "rttypes.h"

typedef struct rt_uart_ls2k
{
    void *base;
    rt_uint32_t irq_num;
} rt_uart_ls2k_t;

void rt_uart_ls2k_set_init(void);

int rt_serial_device_init_param_get(int index, rt_uart_ls2k_t **uart_data,
                                    struct rt_serial_device **serial);
rt_uart_ls2k_t *rt_uart_ls2k_index_of(int index);
struct rt_serial_device *rt_serial_device_index_of(int index);

unsigned char get_uart_div_hi(unsigned int baudrate);
unsigned char get_uart_div_lo(unsigned int baudrate);

int uart_dev_irq_shared_check(int index);
struct rt_serial_device *irq_shared_get_serial_by_index(int irq, int index);

void rt_serial_device_init_internal(int index);

#endif
