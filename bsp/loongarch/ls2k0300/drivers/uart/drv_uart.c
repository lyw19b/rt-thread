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
 *   LS2K0300 development BSP: drv uart.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "rttypes.h"

#include <loongson_board_info.h>
#include <loongson_soc_info.h>
#include <drv_uart.h>
#include <drv_interrupt.h>

#include <drv_uart_custom.h>

// set baud rate
static rt_err_t ls2k_uart_set_baud(void *uart_base, struct serial_configure *cfg)
{
    unsigned char lcr_ori;

    // 设置波特率
    lcr_ori = UART_LCR(uart_base);
    UART_LCR(uart_base) = 0x80;
    UART_MSB(uart_base) = get_uart_div_hi(cfg->baud_rate);
    UART_LSB(uart_base) = get_uart_div_lo(cfg->baud_rate);
    UART_LCR(uart_base) = lcr_ori;

    return RT_EOK;
}

static rt_err_t ls2k_uart_set_frame_format(void *uart_base, struct serial_configure *cfg)
{
    unsigned char lcr_val = 0;

    lcr_val = 0x3;
    if (5 == cfg->data_bits)
    {
        lcr_val = 0x0;
    }
    else if (6 == cfg->data_bits)
    {
        lcr_val = 0x1;
    }
    else if (7 == cfg->data_bits)
    {
        lcr_val = 0x2;
    }
    else if (8 == cfg->data_bits)
    {
        lcr_val = 0x3;
    }

    if (1 == cfg->stop_bits)
    {
        lcr_val |= 0;
    }
    else if (2 == cfg->stop_bits)
    {
        lcr_val |= UART_LCR_SB;
    }

    if (PARITY_NONE == cfg->parity)
    {
        lcr_val |= 0;
    }
    else if ((PARITY_EVEN == cfg->parity))
    {
        lcr_val |= UART_LCR_PE;
        lcr_val |= UART_LCR_SPB;
    }
    else if ((PARITY_ODD == cfg->parity))
    {
        lcr_val |= UART_LCR_PE;
        lcr_val |= UART_LCR_EPS;
        lcr_val |= UART_LCR_SPB;
    }
    UART_LCR(uart_base) = lcr_val;

    return RT_EOK;
}

// configure uart
static rt_err_t _uart_configure(struct rt_serial_device *serial, struct serial_configure *cfg)
{
    struct rt_uart_ls2k *uart_dev;
    void *uart_base;

    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
    uart_base = uart_dev->base;

    // reset fifo and setup trigger size is 14
    UART_FCR(uart_dev->base) = 0xc3;
    UART_FCR(uart_dev->base) = 0xc0;

    // set baud rate
    ls2k_uart_set_baud(uart_base, cfg);

    // such as: 8 bits, 1 stop, no parity
    ls2k_uart_set_frame_format(uart_base, cfg);

    return RT_EOK;
}

// use cmd to control uart
static rt_err_t _uart_control(struct rt_serial_device *serial, int cmd, void *arg)
{
    struct rt_uart_ls2k *uart_dev;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;

    switch (cmd)
    {
    // disable interrupt
    case RT_DEVICE_CTRL_CLR_INT:
        if ((size_t)arg == RT_DEVICE_FLAG_INT_RX)
        {
            // rt_hw_interrupt_mask(uart_dev->irq_num);
            // disable IRxE and ILE
            UART_IER(uart_dev->base) &= ~(UART_IER_IRxE | UART_IER_ILE);
        }
        break;

    // enable interrupt
    case RT_DEVICE_CTRL_SET_INT:
        if ((size_t)arg == RT_DEVICE_FLAG_INT_RX)
        {
            rt_hw_interrupt_umask(uart_dev->irq_num);
            // enable IRxE and ILE
            UART_IER(uart_dev->base) |= (UART_IER_IRxE | UART_IER_ILE);
        }
        break;

    case RT_DEVICE_CTRL_CLOSE:
        UART_IER(uart_dev->base) &= ~(UART_IER_IRxE | UART_IER_ILE);
        break;
    // unsupported cmd
    default:
        rt_kprintf("_uart_control: Unsupported uart cmd\n");
        break;
    }

    return RT_EOK;
}

static rt_bool_t uart_is_transmit_empty(struct rt_uart_ls2k *uart_dev)
{
    rt_uint8_t status = UART_LSR(uart_dev->base);
    if (status & (UART_LSR_TE | UART_LSR_TFE))
    {
        return RT_TRUE;
    }
    else
    {
        return RT_FALSE;
    }
}

// uart put char
static int _uart_putc(struct rt_serial_device *serial, char c)
{
    struct rt_uart_ls2k *uart_dev = RT_NULL;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
    while (RT_NULL == uart_is_transmit_empty(uart_dev))
        ;
    UART_DAT(uart_dev->base) = c;
    return 1;
}

// uart get char
static int _uart_getc(struct rt_serial_device *serial)
{
    struct rt_uart_ls2k *uart_dev = RT_NULL;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
    if (LSR_RXRDY & UART_LSR(uart_dev->base))
    {
        return UART_DAT(uart_dev->base);
    }
    return -1;
}

static int _dma_transmit(struct rt_serial_device *serial, rt_uint8_t *buf, rt_size_t size,
                         int direction)
{
    return -1;
}

static void uart_irq_handle_rx(struct rt_serial_device *serial, struct rt_uart_ls2k *uart_dev)
{
    unsigned char iir;

    iir = UART_IIR(uart_dev->base);
    /* Find out interrupt reason */
    if ((IIR_RXTOUT & iir) || (IIR_RXRDY & iir))
    {
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_RX_IND);
    }
}

// uart irq handler
static void uart_irq_handler(int vector, void *param)
{
    struct rt_serial_device *serial;
    struct rt_uart_ls2k *uart_dev;

    serial = (struct rt_serial_device *)param;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
    uart_irq_handle_rx(serial, uart_dev);
}

static void uart_irq_shared_handler(int vector, void *param)
{
    int index;
    struct rt_serial_device *serial;
    struct rt_uart_ls2k *uart_dev;
    (void)param;

    for (index = 0;; ++index)
    {
        serial = irq_shared_get_serial_by_index(vector, index);
        if (!serial)
        {
            break;
        }
        uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
        uart_irq_handle_rx(serial, uart_dev);
    }
}

static const struct rt_uart_ops ls2k_uart_ops = { .configure = _uart_configure,
                                                  .control = _uart_control,
                                                  .putc = _uart_putc,
                                                  .getc = _uart_getc,
                                                  .dma_transmit = RT_NULL };

static void rt_hw_uart_init_register_to_rt(int index, const char *uart_name)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    struct rt_uart_ls2k *uart = RT_NULL;
    struct rt_serial_device *serial_device = RT_NULL;

    rt_serial_device_init_param_get(index, &uart, &serial_device);
    serial_device->ops = &ls2k_uart_ops;
    serial_device->config = config;
    rt_serial_device_init_internal(index);
    rt_hw_serial_register(serial_device, uart_name, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                          uart);
    if (!uart_dev_irq_shared_check(index))
    {
        rt_hw_interrupt_install(uart->irq_num, uart_irq_shared_handler, RT_NULL, uart_name);
    }
    else
    {
        rt_hw_interrupt_install(uart->irq_num, uart_irq_handler, serial_device, uart_name);
    }
}

void rt_hw_uart_init(void)
{
    rt_uart_ls2k_set_init();

    // Kconfig custom which uart dev will be register
    rt_hw_uart_init_register_to_rt(0, "uart0");
    rt_hw_uart_init_register_to_rt(1, "uart1");
    rt_hw_uart_init_register_to_rt(2, "uart2");
    rt_hw_uart_init_register_to_rt(3, "uart3");
    rt_hw_uart_init_register_to_rt(4, "uart4");
    rt_hw_uart_init_register_to_rt(5, "uart5");
    rt_hw_uart_init_register_to_rt(6, "uart6");
    rt_hw_uart_init_register_to_rt(7, "uart7");
    rt_hw_uart_init_register_to_rt(8, "uart8");
    rt_hw_uart_init_register_to_rt(9, "uart9");
}
