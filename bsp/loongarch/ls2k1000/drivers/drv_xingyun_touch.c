/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    add physical Xingyun Goodix touch bus
 *
 * Description:
 *   Poll the LS2K1000LA byte-register I2C1 controller at 100 kHz. The board
 *   DTS calls this bus i2c3. Preserve the panel configuration and LCD routing.
 *   RT-Thread serializes transfers; each transfer has a bounded deadline.
 */

#include <rtthread.h>
#include <rthw.h>

#ifdef BSP_USING_XINGYUN_TOUCH
#include <drivers/dev_i2c.h>
#include "ls2k0300_display.h"

#define I2C_BASE       0x800000001fe21800UL
#define PINMUX        0x800000001fe00420UL
#define I2C1_ENABLE   (1U << 11)
#define PRELOW        0
#define PREHIGH       1
#define CONTROL       2
#define DATA          3
#define COMMAND       4
#define STATUS        4
#define ENABLE        0x80
#define START         0x91
#define STOP          0x41
#define WRITE         0x11
#define READ_ACK      0x21
#define READ_NACK     0x29
#define IACK          0x01
#define COMPLETE      0x01
#define IN_PROGRESS   0x02
#define ARBITRATION   0x20
#define BUSY          0x40
#define NACK          0x80
#define PRESCALER     (125000000U / (5U * 100000U) - 1U)

static struct rt_i2c_bus_device touch_bus;
static rt_uint32_t transfers;
static rt_uint32_t failures;
static rt_bool_t bus_registered;

static rt_uint8_t i2c_read(unsigned int reg)
{
    rt_uint8_t value = HWREG8(I2C_BASE + reg);

    rt_hw_dmb();
    return value;
}

static void i2c_write(unsigned int reg, rt_uint8_t value)
{
    HWREG8(I2C_BASE + reg) = value;
    rt_hw_dmb();
}

static void reset_controller(void)
{
    i2c_write(CONTROL, 0);
    i2c_write(PRELOW, PRESCALER & 0xff);
    i2c_write(PREHIGH, PRESCALER >> 8);
    i2c_write(COMMAND, IACK);
    i2c_write(CONTROL, ENABLE);
}

static int wait_command(rt_tick_t start, rt_bool_t check_ack)
{
    for (;;)
    {
        rt_uint8_t status = i2c_read(STATUS);

        if (status & ARBITRATION)
        {
            return -RT_EBUSY;
        }

        if ((status & (COMPLETE | IN_PROGRESS)) == COMPLETE)
        {
            return check_ack && (status & NACK) ? -RT_EIO : RT_EOK;
        }

        if (rt_tick_get() - start >= touch_bus.timeout)
        {
            return -RT_ETIMEOUT;
        }
    }
}

static int stop_transfer(void)
{
    rt_tick_t start = rt_tick_get();

    i2c_write(COMMAND, STOP);

    while (i2c_read(STATUS) & BUSY)
    {
        if (rt_tick_get() - start >= touch_bus.timeout)
        {
            reset_controller();
            return -RT_ETIMEOUT;
        }
    }

    i2c_write(COMMAND, IACK);
    return RT_EOK;
}

static rt_ssize_t touch_transfer(struct rt_i2c_bus_device *bus,
                                struct rt_i2c_msg *messages, rt_uint32_t count)
{
    rt_tick_t start = rt_tick_get();
    rt_uint32_t index;
    int error = RT_EOK;
    int stop_error;

    RT_UNUSED(bus);

    for (index = 0; index < count; index++)
    {
        if (messages[index].addr > 127 || !messages[index].buf ||
            !messages[index].len || (messages[index].flags & ~RT_I2C_RD))
        {
            return -RT_EINVAL;
        }
    }

    if (!count)
    {
        return 0;
    }

    transfers++;

    if (i2c_read(STATUS) & BUSY)
    {
        failures++;
        return -RT_EBUSY;
    }

    for (index = 0; index < count && !error; index++)
    {
        struct rt_i2c_msg *message = &messages[index];
        rt_bool_t reading = !!(message->flags & RT_I2C_RD);
        rt_uint32_t byte;

        i2c_write(DATA, (message->addr << 1) | reading);
        i2c_write(COMMAND, START);
        error = wait_command(start, RT_TRUE);

        for (byte = 0; byte < message->len && !error; byte++)
        {
            if (reading)
            {
                i2c_write(COMMAND, byte + 1 == message->len ? READ_NACK : READ_ACK);
                error = wait_command(start, RT_FALSE);

                if (!error)
                {
                    message->buf[byte] = i2c_read(DATA);
                }
            }
            else
            {
                i2c_write(DATA, message->buf[byte]);
                i2c_write(COMMAND, WRITE);
                error = wait_command(start, RT_TRUE);
            }
        }
    }

    /* After arbitration loss the winning master owns STOP. */
    if (error == -RT_EBUSY)
    {
        i2c_write(COMMAND, IACK);
    }
    else
    {
        stop_error = stop_transfer();

        if (!error)
        {
            error = stop_error;
        }
    }

    if (error)
    {
        failures++;
        return error;
    }

    return count;
}

static const struct rt_i2c_bus_device_ops touch_bus_ops =
{
    .master_xfer = touch_transfer,
};

static int xingyun_touch_init(void)
{
    int error;

    HWREG32(PINMUX) |= I2C1_ENABLE;
    rt_hw_dmb();
    reset_controller();
    touch_bus.ops = &touch_bus_ops;
    touch_bus.timeout = rt_tick_from_millisecond(100);
    error = rt_i2c_bus_device_register(&touch_bus, "i2c1");

    if (error)
    {
        return error;
    }

    bus_registered = RT_TRUE;
    error = laxpu_gt9xx_register_polled("i2c1", 0x5d);

    if (error)
    {
        error = laxpu_gt9xx_register_polled("i2c1", 0x14);
    }

    rt_kprintf("Xingyun touch: I2C1 100 kHz, result=%d, status=0x%02x\n",
               error, i2c_read(STATUS));
    return error;
}
INIT_DEVICE_EXPORT(xingyun_touch_init);

static void xingyun_touch_status(void)
{
    if (!bus_registered)
    {
        rt_kprintf("Xingyun touch: I2C bus is not registered\n");
        return;
    }

    rt_i2c_bus_lock(&touch_bus, RT_WAITING_FOREVER);
    rt_kprintf("Xingyun touch: transfers=%u failures=%u status=0x%02x\n",
               transfers, failures, i2c_read(STATUS));
    rt_i2c_bus_unlock(&touch_bus);
}
MSH_CMD_EXPORT(xingyun_touch_status, Show touch bus counters without consuming reports);
#endif
