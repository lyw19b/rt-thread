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
 *   Register HPET measurements while keeping CPU timer tick and firmware state intact.
 * Notes:
 *   CPU timers retain the system tick; this device provides no comparator IRQ callbacks.
 */

#include <rtdevice.h>
#include "periph_resource.h"
#include "hpet_counter.h"

static struct ls_periph_io hpet_io;
static struct rt_device hpet_device;

static rt_ssize_t hpet_read(rt_device_t device, rt_off_t position, void *buffer, rt_size_t size)
{
    uint64_t ticks;
    int result;

    (void)device;

    if (position || !buffer || size != sizeof(ticks))
    {
        return -RT_EINVAL;
    }

    result = ls_hpet_read(&hpet_io, &ticks);

    if (result)
    {
        return ls2k3000_periph_error(result);
    }

    rt_memcpy(buffer, &ticks, sizeof(ticks));
    return sizeof(ticks);
}

static rt_err_t hpet_control(rt_device_t device, int command, void *argument)
{
    (void)device;

    if (command != LS2K3000_HPET_GET_PERIOD || !argument)
    {
        return -RT_EINVAL;
    }

    return ls2k3000_periph_error(ls_hpet_period(&hpet_io, argument));
}

static const struct rt_device_ops hpet_ops = {
    .read = hpet_read,
    .control = hpet_control,
};

static int ls2k3000_hpet_init(void)
{
    uint32_t period;
    int result = ls2k3000_periph_map(0x7a42, 0x40000, 0x150, &hpet_io);

    if (result)
    {
        return result;
    }

    result = ls_hpet_period(&hpet_io, &period);

    if (result)
    {
        return ls2k3000_periph_error(result);
    }

    /* Do not enable an inherited comparator, reset the counter or steal tick. */
    hpet_device.type = RT_Device_Class_Miscellaneous;
    hpet_device.ops = &hpet_ops;
    return rt_device_register(&hpet_device, "hpet0", RT_DEVICE_FLAG_RDONLY);
}
INIT_DEVICE_EXPORT(ls2k3000_hpet_init);
