#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "rttypes.h"

#include <ls2k1000la.h>
#include <drv_uart.h>
#include <drv_interrupt.h>

/*
// uart configuration
const struct serial_configure config_uart = {
    .baud_rate = BAUD_RATE_115200,           // 115200 bits/s
    .data_bits = DATA_BITS_8,                // 8 data bits
    .stop_bits = STOP_BITS_1,                // 1 stop bit
    .parity = PARITY_NONE,                   // No parity
    .bit_order = BIT_ORDER_LSB,              // LSB first sent
    .invert = NRZ_NORMAL,                    // Normal mode
    .bufsz = RT_SERIAL_RB_BUFSZ,             // Buffer size
    .flowcontrol = RT_SERIAL_FLOWCONTROL_NONE // Off flowcontrol
};
*/

struct rt_uart_ls2k
{
    void *base;
    rt_uint32_t irq_num;
};

struct rt_uart_ls2k uart_dev0 =
{
    .base = (void *)LS_UART0_REG_BASE,
    .irq_num = LS2K_UART_0123_IRQ,
};

struct rt_uart_ls2k uart_dev4 =
{
    .base = (void *)LS_UART4_REG_BASE,
    .irq_num = LS2K_UART_4567_IRQ,
};

struct rt_serial_device serial[12];

// set baud rate
static rt_err_t ls2k_uart_set_baud(struct rt_serial_device *serial, struct serial_configure *cfg)
{
    struct rt_uart_ls2k *uart_dev;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;

    // uart use apb clock, 125MHz default
    rt_uint64_t apb_clock = 125000000;
    rt_uint64_t brtc = apb_clock / (16 * (cfg->baud_rate));

    // activate divisor latch
    UART_LCR(uart_dev->base) |= UART_LCR_BKSE;

    UART_LSB(uart_dev->base) = brtc & 0xff;
    UART_MSB(uart_dev->base) = (brtc >> 8) & 0xff;

    // deactivate divisor latch
    UART_LCR(uart_dev->base) &= ~UART_LCR_BKSE;

    return RT_EOK;
}

// configure uart
static rt_err_t _uart_configure(struct rt_serial_device *serial, struct serial_configure *cfg)
{
    struct rt_uart_ls2k *uart_dev;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;

    // disable all interrupts
    UART_IER(uart_dev->base) = 0x00;

    // see 0x1fe00428 for multi-port support

    // set baud rate
    ls2k_uart_set_baud(serial, cfg);

    // reset fifo
    UART_FCR(uart_dev->base) = UART_FCR_FIFO_EN | UART_FCR_FIFO_CLEAR;

    // 8 bits, 1 stop, no parity
    UART_LCR(uart_dev->base) = UART_LCR_WLS_8;

    // dtr and rts
    UART_MCR(uart_dev->base) = UART_MCR_DTR | UART_MCR_RTS;

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
            rt_hw_interrupt_mask(uart_dev->irq_num);
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
    while (RT_NULL == uart_is_transmit_empty(uart_dev));
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

static int _dma_transmit(struct rt_serial_device *serial, rt_uint8_t *buf, rt_size_t size, int direction)
{
    return -1;
}

// uart irq handler
static void uart_irq_handler(int vector, void *param)
{
    struct rt_serial_device *serial = (struct rt_serial_device *)param;
    struct rt_uart_ls2k *uart_dev = RT_NULL;
    uart_dev = (struct rt_uart_ls2k *)serial->parent.user_data;
    unsigned char iir = UART_IIR(uart_dev->base);
    /* Find out interrupt reason */
    if ((IIR_RXTOUT & iir) || (IIR_RXRDY & iir))
    {
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_RX_IND);
    }
}

static const struct rt_uart_ops ls2k_uart_ops =
{
    .configure = _uart_configure,
    .control = _uart_control,
    .putc = _uart_putc,
    .getc = _uart_getc,
    .dma_transmit = RT_NULL
};

void rt_hw_uart_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    struct rt_uart_ls2k *uart = RT_NULL;

#ifdef RT_USING_UART0
    uart = &uart_dev0;
    serial[0].ops   = &ls2k_uart_ops;
    serial[0].config = config;
    rt_hw_serial_register(&serial[0],
                          "uart0",
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                          uart);
    rt_hw_interrupt_install(uart->irq_num, uart_irq_handler, &serial[0], "uart0");
#endif

#ifdef RT_USING_UART4
    uart = &uart_dev4;
    serial[4].ops   = &ls2k_uart_ops;
    serial[4].config = config;
    rt_hw_serial_register(&serial[4],
                          "uart4",
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                          uart);
    rt_hw_interrupt_install(uart->irq_num, uart_irq_handler, &serial[4], "uart4");
#endif
}
