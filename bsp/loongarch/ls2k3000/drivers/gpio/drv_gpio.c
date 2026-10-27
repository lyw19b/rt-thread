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
 *   Expose byte-addressed GPIO without changing shared pad mux or inventing pull modes.
 * Notes:
 *   Firmware owns pad mux and pulls; GPIO interrupts are not implemented.
 */

#include <rtdevice.h>
#include "periph_resource.h"

static struct ls_periph_io gpio_io;

static int gpio_valid(rt_base_t pin)
{
    return (pin >= 0 && pin <= 39) || (pin >= 46 && pin <= 63);
}

static void gpio_mode(struct rt_device *device, rt_base_t pin, rt_uint8_t mode)
{
    (void)device;

    if (!gpio_valid(pin) || (mode != PIN_MODE_INPUT && mode != PIN_MODE_OUTPUT))
    {
        rt_set_errno(-RT_EINVAL);
        return;
    }

    /* Each pin has its own byte; changing one pin cannot lose another CPU's write. */
    ls_write(&gpio_io, 0x800 + pin, 1, mode == PIN_MODE_INPUT);
}

static void gpio_write(struct rt_device *device, rt_base_t pin, rt_uint8_t value)
{
    (void)device;

    if (!gpio_valid(pin) || value > 1)
    {
        rt_set_errno(-RT_EINVAL);
        return;
    }

    /* A caller can preload the output latch before enabling the output driver. */
    ls_write(&gpio_io, 0x900 + pin, 1, value);
}

static rt_ssize_t gpio_read(struct rt_device *device, rt_base_t pin)
{
    (void)device;

    if (!gpio_valid(pin))
    {
        return -RT_EINVAL;
    }

    return ls_read(&gpio_io, 0xa00 + pin, 1) & 1;
}

static const struct rt_pin_ops gpio_ops = {
    .pin_mode = gpio_mode,
    .pin_write = gpio_write,
    .pin_read = gpio_read,
};

static int ls2k3000_gpio_init(void)
{
    int result = ls2k3000_periph_map(0x7a42, 0x60000, 0x1000, &gpio_io);

    if (result)
    {
        return result;
    }

    return rt_device_pin_register("pin", &gpio_ops, RT_NULL);
}
INIT_DEVICE_EXPORT(ls2k3000_gpio_init);
