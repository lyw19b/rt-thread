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

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "rttypes.h"

#include "loongson_board_info.h"
#include "loongson_soc_info.h"
#include <drv_interrupt.h>

#include "drv_uart_custom.h"

#define UART_DEV_SIZE (10)

static rt_uart_ls2k_t uart_dev_set[UART_DEV_SIZE];
static struct rt_serial_device serial[UART_DEV_SIZE];
static unsigned int serial_register_bit_map;

/*
 * 具体芯片相关
 * 不同芯片需要移植
 */

static void rt_uart_ls2k_init(int index, void *base, unsigned int irq_num)
{
    // invalid
    if (index < 0 || index >= UART_DEV_SIZE)
    {
        return;
    }
    if (!base)
    {
        return;
    }
    uart_dev_set[index].base = base;
    uart_dev_set[index].irq_num = irq_num;
}

void rt_uart_ls2k_set_init(void)
{
    unsigned int general_cfg = 0;

    general_cfg = readl(LS_GENERAL_CFG0);
    general_cfg |= (0xff << 20);
    writel(general_cfg, LS_GENERAL_CFG0);

    serial_register_bit_map = 0;

    rt_uart_ls2k_init(0, LS_UART0_REG_BASE, LS2K0300_LIO_UART0_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(1, LS_UART1_REG_BASE, LS2K0300_LIO_UART1_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(2, LS_UART2_REG_BASE, LS2K0300_LIO_UART2_5_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(3, LS_UART3_REG_BASE, LS2K0300_LIO_UART2_5_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(4, LS_UART4_REG_BASE, LS2K0300_LIO_UART2_5_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(5, LS_UART5_REG_BASE, LS2K0300_LIO_UART2_5_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(6, LS_UART6_REG_BASE, LS2K0300_LIO_UART6_9_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(7, LS_UART7_REG_BASE, LS2K0300_LIO_UART6_9_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(8, LS_UART8_REG_BASE, LS2K0300_LIO_UART6_9_IRQ + SOC_LIO_IRQ_START);
    rt_uart_ls2k_init(9, LS_UART9_REG_BASE, LS2K0300_LIO_UART6_9_IRQ + SOC_LIO_IRQ_START);
}

int rt_serial_device_init_param_get(int index, rt_uart_ls2k_t **uart_data,
                                    struct rt_serial_device **serial)
{
    if (index < 0 || index >= UART_DEV_SIZE)
    {
        return 1;
    }
    if (!uart_data)
    {
        return 1;
    }
    if (!serial)
    {
        return 1;
    }
    uart_data[0] = rt_uart_ls2k_index_of(index);
    serial[0] = rt_serial_device_index_of(index);

    return 0;
}

void rt_serial_device_init_internal(int index)
{
    if (index < 0 || index >= UART_DEV_SIZE)
    {
        return;
    }
    serial_register_bit_map |= (1 << index);
}

rt_uart_ls2k_t *rt_uart_ls2k_index_of(int index)
{
    if (index < 0 || index >= UART_DEV_SIZE)
    {
        return NULL;
    }
    return uart_dev_set + index;
}

struct rt_serial_device *rt_serial_device_index_of(int index)
{
    if (index < 0 || index >= UART_DEV_SIZE)
    {
        return NULL;
    }
    return serial + index;
}

/*
 * @brief: 计算串口波特率分频值的高 8bit
 * @param: baudrate: 波特率
 */
unsigned char get_uart_div_hi(unsigned int baudrate)
{
    unsigned char div_hi;

    div_hi = (((UART_FREQ * 1000000) + (baudrate * 8)) / (baudrate * 16)) >> 8;

    return div_hi;
}

/*
 * @brief: 计算串口波特率分频值的低 8bit
 * @param: baudrate: 波特率
 */
unsigned char get_uart_div_lo(unsigned int baudrate)
{
    unsigned char div_lo;

    div_lo = (((UART_FREQ * 1000000) + (baudrate * 8)) / (baudrate * 16)) & 0xff;

    return div_lo;
}

int uart_dev_irq_shared_check(int index)
{
    // uart 0 1
    if (index < 2)
    {
        return 1;
    }
    return 0;
}

struct rt_serial_device *irq_shared_get_serial_by_index(int irq, int index)
{
    int serial_index;
    if (index < 0 || index >= 4)
    {
        return RT_NULL;
    }
    if (irq == LS2K0300_LIO_UART2_5_IRQ)
    {
        serial_index = index + 2;
    }
    else if (irq == LS2K0300_LIO_UART6_9_IRQ)
    {
        serial_index = index + 6;
    }
    else
    {
        return RT_NULL;
    }

    if (serial_register_bit_map & (1 << serial_index))
    {
        return rt_serial_device_index_of(serial_index);
    }
    else
    {
        return RT_NULL;
    }
}
