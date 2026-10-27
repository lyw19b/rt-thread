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
 *   Register the battery-backed RTC with SMP serialization and UTC semantics.
 * Notes:
 *   Registration preserves battery time; hardware and software RTC cannot coexist.
 */

#include <rtdevice.h>
#include <sys/time.h>
#include "periph_resource.h"
#include "rtc_toy.h"

#ifdef RT_USING_SOFT_RTC
#error "Disable software RTC when using LS2K3000 TOY RTC"
#endif

static struct ls_periph_io rtc_io;
static struct rt_mutex rtc_lock;
static rt_rtc_dev_t rtc_device;

static rt_err_t rtc_get_value(struct timeval *value)
{
    int64_t seconds;
    unsigned int tenths;
    int result;

    if (!value || rt_interrupt_get_nest())
    {
        return -RT_EINVAL;
    }

    result = rt_mutex_take(&rtc_lock, RT_WAITING_FOREVER);

    if (result)
    {
        return result;
    }

    result = ls_rtc_read(&rtc_io, &seconds, &tenths);
    rt_mutex_release(&rtc_lock);

    if (!result)
    {
        value->tv_sec = seconds;
        value->tv_usec = tenths * 100000;
    }

    return ls2k3000_periph_error(result);
}

static rt_err_t rtc_set_value(struct timeval *value)
{
    int result;

    if (!value || value->tv_usec < 0 || value->tv_usec >= 1000000 || rt_interrupt_get_nest())
    {
        return -RT_EINVAL;
    }

    result = rt_mutex_take(&rtc_lock, RT_WAITING_FOREVER);

    if (result)
    {
        return result;
    }

    result = ls_rtc_write(&rtc_io, value->tv_sec, value->tv_usec / 100000);
    rt_mutex_release(&rtc_lock);
    return ls2k3000_periph_error(result);
}

static rt_err_t rtc_get_seconds(time_t *seconds)
{
    struct timeval value;
    int result;

    if (!seconds)
    {
        return -RT_EINVAL;
    }

    result = rtc_get_value(&value);

    if (!result)
    {
        *seconds = value.tv_sec;
    }

    return result;
}

static rt_err_t rtc_set_seconds(time_t *seconds)
{
    struct timeval value;

    if (!seconds)
    {
        return -RT_EINVAL;
    }

    value.tv_sec = *seconds;
    value.tv_usec = 0;
    return rtc_set_value(&value);
}

static rt_err_t rtc_init(void)
{
    return RT_EOK;
}

static const struct rt_rtc_ops rtc_ops = {
    .init = rtc_init,
    .get_secs = rtc_get_seconds,
    .set_secs = rtc_set_seconds,
    .get_timeval = rtc_get_value,
    .set_timeval = rtc_set_value,
};

static int ls2k3000_rtc_init(void)
{
    int result = ls2k3000_periph_map(0x7a42, 0x50100, 0x80, &rtc_io);

    if (result)
    {
        return result;
    }

    result = rt_mutex_init(&rtc_lock, "rtc", RT_IPC_FLAG_PRIO);

    if (result)
    {
        return result;
    }

    rtc_device.ops = &rtc_ops;
    result = rt_hw_rtc_register(&rtc_device, "rtc", RT_DEVICE_FLAG_RDWR, RT_NULL);

    if (result)
    {
        rt_mutex_detach(&rtc_lock);
    }

    return result;
}
INIT_DEVICE_EXPORT(ls2k3000_rtc_init);
