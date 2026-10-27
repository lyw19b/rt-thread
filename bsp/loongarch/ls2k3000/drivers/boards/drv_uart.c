/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Use bounded UART0 TX and software-polled RX during interrupt bring-up.
 */

#include <rtdevice.h>
#include <rthw.h>
#include "board.h"

#define UART_BASE LOONGARCH_PHYS_UNCACHED(0x1fe001e0UL)

static struct rt_serial_device serial;
static rt_uint32_t receive_enabled;
static struct rt_spinlock uart_lock;

static rt_uint8_t read_reg(unsigned int reg)
{
    return *(volatile rt_uint8_t *)(UART_BASE + reg);
}

static void write_reg(unsigned int reg, rt_uint8_t value)
{
    *(volatile rt_uint8_t *)(UART_BASE + reg) = value;
    rt_hw_dmb();
}

void ls2k3000_early_puts(const char *text)
{
    while (*text)
    {
        unsigned int budget = 1000000;

        while (!(read_reg(5) & 0x20) && --budget)
        {
            __asm__ volatile("" ::: "memory");
        }

        if (!budget)
        {
            return;
        }

        write_reg(0, *text++);
    }
}

static rt_err_t configure(struct rt_serial_device *device, struct serial_configure *config)
{
    rt_uint64_t divisor;
    rt_base_t level;

    RT_UNUSED(device);

    if (config->baud_rate != 115200 || config->data_bits != DATA_BITS_8 ||
        config->stop_bits != STOP_BITS_1 || config->parity != PARITY_NONE ||
        config->flowcontrol != RT_SERIAL_FLOWCONTROL_NONE ||
        config->bit_order != BIT_ORDER_LSB || config->invert != NRZ_NORMAL)
    {
        return -RT_EINVAL;
    }

    divisor = ((rt_uint64_t)BSP_UART_CLOCK_HZ + 8 * config->baud_rate) /
               (16 * config->baud_rate);

    if (!divisor || divisor > 0xffff)
    {
        return -RT_EINVAL;
    }

    level = rt_spin_lock_irqsave(&uart_lock);
    write_reg(3, 3);
    write_reg(1, 0);
    write_reg(3, 0x83);
    write_reg(0, divisor & 0xff);
    write_reg(1, divisor >> 8);
    write_reg(3, 3);
    write_reg(2, 7);
    rt_spin_unlock_irqrestore(&uart_lock, level);
    return RT_EOK;
}

static rt_err_t control(struct rt_serial_device *device, int command, void *argument)
{
    RT_UNUSED(device);

    if ((rt_ubase_t)argument != RT_DEVICE_FLAG_INT_RX)
    {
        return -RT_EINVAL;
    }

    if (command == RT_DEVICE_CTRL_SET_INT || command == RT_DEVICE_CTRL_CLR_INT)
    {
        __atomic_store_n(&receive_enabled, command == RT_DEVICE_CTRL_SET_INT,
                         __ATOMIC_RELEASE);
        return RT_EOK;
    }

    return -RT_ENOSYS;
}

static int put_char(struct rt_serial_device *device, char value)
{
    RT_UNUSED(device);

    for (unsigned int budget = 0; budget < 1000000; ++budget)
    {
        rt_base_t level = rt_spin_lock_irqsave(&uart_lock);

        if (read_reg(5) & 0x20)
        {
            write_reg(0, value);
            rt_spin_unlock_irqrestore(&uart_lock, level);
            return 1;
        }

        rt_spin_unlock_irqrestore(&uart_lock, level);
    }

    return -1;
}

static int get_char(struct rt_serial_device *device)
{
    rt_base_t level = rt_spin_lock_irqsave(&uart_lock);
    int value = (read_reg(5) & 1) ? read_reg(0) : -1;

    RT_UNUSED(device);
    rt_spin_unlock_irqrestore(&uart_lock, level);
    return value;
}

static const struct rt_uart_ops uart_ops = {
    .configure = configure,
    .control = control,
    .putc = put_char,
    .getc = get_char,
};

static void poll_receive(void *argument)
{
    RT_UNUSED(argument);

    for (;;)
    {
        if (__atomic_load_n(&receive_enabled, __ATOMIC_ACQUIRE) && (read_reg(5) & 1))
        {
            rt_hw_serial_isr(&serial, RT_SERIAL_EVENT_RX_IND);
        }

        rt_thread_mdelay(1);
    }
}

static int start_receiver(void)
{
    rt_thread_t thread = rt_thread_create("uart_poll", poll_receive, RT_NULL,
                                          2048, 16, 5);

    if (!thread)
    {
        return -RT_ENOMEM;
    }

    return rt_thread_startup(thread);
}

INIT_DEVICE_EXPORT(start_receiver);

void ls2k3000_uart_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t result;

    rt_spin_lock_init(&uart_lock);
    serial.ops = &uart_ops;
    serial.config = config;
    result = rt_hw_serial_register(&serial, "uart",
                                   RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX, RT_NULL);
    RT_ASSERT(result == RT_EOK);
}
