/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rthw.h>
#include "ls2k_uart.h"
#ifndef RT_USING_NANO
#include <rtdevice.h>
#endif

#define UART_DATA   0
#define UART_IER    1
#define UART_IIR    2
#define UART_FCR    2
#define UART_LCR    3
#define UART_MCR    4
#define UART_LSR    5
#define UART_DLAB   0x80
#define UART_RX     0x01
#define UART_TX     0x20

static const struct ls2k_uart_config *ports;
static rt_size_t port_count;

static rt_uint8_t _read(const struct ls2k_uart_config *port, unsigned int reg)
{
    return HWREG8(port->base + reg);
}

static void _write(const struct ls2k_uart_config *port, unsigned int reg, rt_uint8_t value)
{
    HWREG8(port->base + reg) = value;
    rt_hw_dmb();
}

static rt_err_t _set_format(const struct ls2k_uart_config *port, rt_uint32_t baud,
                            rt_uint8_t format)
{
    rt_uint64_t divisor;
    rt_uint8_t interrupts;
    rt_base_t level;

    if (!baud || !port->clock)
        return -RT_EINVAL;
    divisor = ((rt_uint64_t)port->clock + 8ULL * baud) / (16ULL * baud);
    if (!divisor || divisor > 0xffff)
        return -RT_EINVAL;
    /* DLAB aliases the data/IER registers. Keep an RX ISR from accessing
     * the divisor while a thread is changing the serial configuration. */
    level = rt_hw_interrupt_disable();
    interrupts = _read(port, UART_IER);
    _write(port, UART_IER, 0);
    _write(port, UART_LCR, format | UART_DLAB);
    _write(port, UART_DATA, divisor & 0xff);
    _write(port, UART_IER, divisor >> 8);
    _write(port, UART_LCR, format);
    _write(port, UART_IER, interrupts);
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

static int _putchar(const struct ls2k_uart_config *port, char c)
{
    while (!(_read(port, UART_LSR) & UART_TX)) {}
    _write(port, UART_DATA, c);
    return 1;
}

#ifdef RT_USING_NANO
void rt_hw_console_output(const char *str)
{
    if (!port_count)
        return;
    while (*str)
    {
        if (*str == '\n')
            _putchar(ports, '\r');
        _putchar(ports, *str++);
    }
}

char rt_hw_console_getchar(void)
{
    if (port_count && (_read(ports, UART_LSR) & UART_RX))
        return _read(ports, UART_DATA);
    rt_thread_mdelay(1);
    return -1;
}
#else
static struct rt_serial_device serials[10];

static rt_err_t _configure(struct rt_serial_device *serial, struct serial_configure *config)
{
    const struct ls2k_uart_config *port = serial->parent.user_data;
    rt_uint8_t format;

    if (config->data_bits < DATA_BITS_5 || config->data_bits > DATA_BITS_8 ||
        (config->stop_bits != STOP_BITS_1 && config->stop_bits != STOP_BITS_2) ||
        config->parity > PARITY_EVEN || config->flowcontrol != RT_SERIAL_FLOWCONTROL_NONE ||
        config->bit_order != BIT_ORDER_LSB || config->invert != NRZ_NORMAL)
        return -RT_EINVAL;
    format = config->data_bits - DATA_BITS_5;
    if (config->stop_bits == STOP_BITS_2) format |= 0x04;
    if (config->parity != PARITY_NONE) format |= 0x08;
    if (config->parity == PARITY_EVEN) format |= 0x10;
    return _set_format(port, config->baud_rate, format);
}

static rt_err_t _control(struct rt_serial_device *serial, int cmd, void *arg)
{
    const struct ls2k_uart_config *port = serial->parent.user_data;
    rt_base_t level;

    if ((rt_ubase_t)arg != RT_DEVICE_FLAG_INT_RX)
        return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    if (cmd == RT_DEVICE_CTRL_SET_INT)
    {
        _write(port, UART_IER, _read(port, UART_IER) | 0x05);
        rt_hw_interrupt_umask(port->irq);
    }
    else if (cmd == RT_DEVICE_CTRL_CLR_INT)
    {
        /* Several UARTs may share one controller IRQ. Mask this UART only. */
        _write(port, UART_IER, _read(port, UART_IER) & ~0x05);
    }
    else
    {
        rt_hw_interrupt_enable(level);
        return -RT_ENOSYS;
    }
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

static int _putc(struct rt_serial_device *serial, char c)
{
    return _putchar(serial->parent.user_data, c);
}

static int _getc(struct rt_serial_device *serial)
{
    const struct ls2k_uart_config *port = serial->parent.user_data;
    return (_read(port, UART_LSR) & UART_RX) ? _read(port, UART_DATA) : -1;
}

static void _irq(int vector, void *parameter)
{
    rt_size_t i;

    RT_UNUSED(parameter);
    for (i = 0; i < port_count; i++)
    {
        unsigned int reason;
        if (ports[i].irq != (unsigned int)vector)
            continue;
        reason = _read(&ports[i], UART_IIR) & 0x0f;
        if (reason == 0x04 || reason == 0x0c || reason == 0x06)
        {
            /* Reading LSR acknowledges line errors, even without RX data. */
            if (_read(&ports[i], UART_LSR) & UART_RX)
                rt_hw_serial_isr(&serials[i], RT_SERIAL_EVENT_RX_IND);
        }
    }
}

static const struct rt_uart_ops uart_ops =
{
    .configure = _configure,
    .control = _control,
    .putc = _putc,
    .getc = _getc,
};
#endif

void rt_hw_uart_init(void)
{
    rt_size_t i;
    rt_err_t error;

    ports = ls2k_uart_get_config(&port_count);
    RT_ASSERT(ports && port_count && port_count <= 10);
    for (i = 0; i < port_count; i++)
    {
        /* Firmware may leave DLAB set; restore the normal register bank. */
        _write(&ports[i], UART_LCR, 0x03);
        _write(&ports[i], UART_IER, 0);
        _write(&ports[i], UART_FCR, ports[i].fifo_control);
        _write(&ports[i], UART_MCR, 0x03);
#ifdef RT_USING_NANO
        error = _set_format(&ports[i], 115200, 0x03);
#else
        struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
        serials[i].ops = &uart_ops;
        serials[i].config = config;
        rt_hw_interrupt_install(ports[i].irq, _irq, RT_NULL, ports[i].name);
        error = rt_hw_serial_register(&serials[i], ports[i].name,
                  RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX, (void *)&ports[i]);
#endif
        if (error)
        {
            RT_ASSERT(error == RT_EOK);
            while (1) {}
        }
    }
}
