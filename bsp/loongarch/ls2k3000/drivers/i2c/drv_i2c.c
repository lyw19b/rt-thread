/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Register four polled I2C buses with RT-Thread bus locking.
 * Notes:
 *   Only thread-context, 7-bit master transfers are supported; firmware owns pad mux.
 */

#include <rtdevice.h>
#include "periph_resource.h"
#include "i2c_poll.h"

struct ls_i2c_bus
{
    struct rt_i2c_bus_device bus;
    struct ls_periph_io io;
    uint32_t input;
};

static struct ls_i2c_bus buses[4];

static rt_ssize_t i2c_transfer(struct rt_i2c_bus_device *bus,
                              struct rt_i2c_msg *messages, rt_uint32_t count)
{
    struct ls_i2c_bus *controller = (struct ls_i2c_bus *)bus;
    int result;

    if (rt_interrupt_get_nest() || !messages || !count)
    {
        return -RT_EINVAL;
    }

    /* Validate the complete list before producing any bus traffic. */
    for (rt_uint32_t i = 0; i < count; ++i)
    {
        if (messages[i].addr > 0x7f || (messages[i].flags & ~RT_I2C_RD) ||
            (messages[i].len && !messages[i].buf))
        {
            return -RT_EINVAL;
        }
    }

    result = ls_i2c_setup(&controller->io, controller->input, bus->config.max_hz);

    if (result)
    {
        return ls2k3000_periph_error(result);
    }

    for (rt_uint32_t i = 0; i < count; ++i)
    {
        struct ls_i2c_message message = {
            .address = messages[i].addr,
            .read = !!(messages[i].flags & RT_I2C_RD),
            .length = messages[i].len,
            .data = messages[i].buf,
        };

        result = ls_i2c_message(&controller->io, &message);

        if (result)
        {
            ls_i2c_recover(&controller->io);
            return ls2k3000_periph_error(result);
        }
    }

    result = ls_i2c_stop(&controller->io);

    if (result)
    {
        ls_i2c_recover(&controller->io);
        return ls2k3000_periph_error(result);
    }

    return count;
}

static const struct rt_i2c_bus_device_ops i2c_ops = {
    .master_xfer = i2c_transfer,
};

static int ls2k3000_i2c_init(void)
{
    int first_error = RT_EOK;

    for (unsigned int i = 0; i < 4; ++i)
    {
        struct ls_i2c_bus *controller = &buses[i];
        char name[8];
        int result;

        if (!(BSP_LS2K3000_I2C_MASK & (1U << i)))
        {
            continue;
        }

        if (i < 2)
        {
            ls2k3000_periph_fixed(0x1fe00120 + i * 0x10, &controller->io);
            controller->input = 100000000;
        }
        else
        {
            result = ls2k3000_periph_map(0x7a42, 0x10000 + (i - 2) * 0x100,
                                        8, &controller->io);

            if (result)
            {
                first_error = result;
                continue;
            }

            controller->input = 48000000;
        }

        rt_snprintf(name, sizeof(name), "i2c%u", i);
        controller->bus.ops = &i2c_ops;
        controller->bus.config.max_hz = 100000;
        result = rt_i2c_bus_device_register(&controller->bus, name);

        if (result)
        {
            first_error = result;
        }
    }

    return first_error;
}
INIT_DEVICE_EXPORT(ls2k3000_i2c_init);
