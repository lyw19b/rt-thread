/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_spi.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Register SPI buses and native chip-select devices without probing or erasing NOR.
 */

#include <rtdevice.h>
#include "periph_resource.h"
#include "spi_poll.h"

struct ls_spi_bus
{
    struct rt_spi_bus bus;
    struct ls_spi_state state;
    struct rt_spi_device devices[4];
};

static struct ls_spi_bus controllers[2];

static rt_err_t spi_configure(struct rt_spi_device *device, struct rt_spi_configuration *config)
{
    struct ls_spi_bus *controller = (struct ls_spi_bus *)device->bus;

    if (rt_interrupt_get_nest())
    {
        return -RT_EBUSY;
    }

    if (config->data_width != 8 || !(config->mode & RT_SPI_MSB) ||
        (config->mode & ~(RT_SPI_MSB | RT_SPI_CPOL | RT_SPI_CPHA)) ||
        ls_spi_divider(BSP_LS2K3000_SPI_INPUT_HZ, config->max_hz) < 0)
    {
        ls_spi_end(&controller->state);
        return -RT_EINVAL;
    }

    /* A prior abandoned CS chain must not leak into a different device. */
    ls_spi_end(&controller->state);
    return RT_EOK;
}

static rt_ssize_t spi_transfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    struct ls_spi_bus *controller = (struct ls_spi_bus *)device->bus;
    struct ls_spi_state *state = &controller->state;
    unsigned int cs = (rt_ubase_t)device->user_data;
    int result;

    if (rt_interrupt_get_nest())
    {
        return -RT_EBUSY;
    }

    if (message->cs_take)
    {
        result = ls_spi_begin(state, BSP_LS2K3000_SPI_INPUT_HZ, device->config.max_hz,
                              device->config.mode & 3, cs);

        if (result)
        {
            ls_spi_end(state);
            return ls2k3000_periph_error(result);
        }
    }

    if (!state->active || state->cs != cs)
    {
        ls_spi_end(state);
        return -RT_EINVAL;
    }

    result = ls_spi_bytes(state, message->send_buf, message->recv_buf, message->length);

    if (message->cs_release || result)
    {
        ls_spi_end(state);
    }

    return result ? ls2k3000_periph_error(result) : (rt_ssize_t)message->length;
}

static const struct rt_spi_ops spi_ops = {
    .configure = spi_configure,
    .xfer = spi_transfer,
};

static int ls2k3000_spi_init(void)
{
    int first_error = RT_EOK;

    for (unsigned int i = 0; i < 2; ++i)
    {
        struct ls_spi_bus *controller = &controllers[i];
        char bus_name[8];
        int result;

        if (!(BSP_LS2K3000_SPI_MASK & (1U << i)))
        {
            continue;
        }

        if (!i)
        {
            ls2k3000_periph_fixed(0x1fe001f0, &controller->state.io);
        }
        else
        {
            result = ls2k3000_periph_map(0x7a1b, 0, 0x10, &controller->state.io);

            if (result)
            {
                first_error = result;
                continue;
            }
        }

        rt_snprintf(bus_name, sizeof(bus_name), "spi%u", i);
        result = rt_spi_bus_register(&controller->bus, bus_name, &spi_ops);

        if (result)
        {
            first_error = result;
            continue;
        }

        for (unsigned int cs = 0; cs < 4; ++cs)
        {
            char name[12];

            rt_snprintf(name, sizeof(name), "spi%ucs%u", i, cs);
            result = rt_spi_bus_attach_device(&controller->devices[cs], name,
                                              bus_name, (void *)(rt_ubase_t)cs);

            if (result)
            {
                first_error = result;
            }
        }
    }

    return first_error;
}
INIT_DEVICE_EXPORT(ls2k3000_spi_init);
