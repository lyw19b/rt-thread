/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_watchdog.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Register an opt-in hardware watchdog and preserve firmware state at probe.
 */

#include <rtdevice.h>
#include "periph_resource.h"
#include "watchdog_hw.h"

#ifdef BSP_USING_LS2K3000_WDT
static struct ls_periph_io wdt_io;
static struct rt_mutex wdt_lock;
static rt_watchdog_t wdt_device;

static rt_err_t wdt_control(rt_watchdog_t *device, int command, void *argument)
{
    int result;

    (void)device;

    if (rt_interrupt_get_nest())
    {
        return -RT_EBUSY;
    }

    result = rt_mutex_take(&wdt_lock, RT_WAITING_FOREVER);

    if (result)
    {
        return result;
    }

    switch (command)
    {
    case RT_DEVICE_CTRL_WDT_SET_TIMEOUT:
        result = argument ? ls_wdt_timeout(&wdt_io, *(rt_uint32_t *)argument) : LS_INVALID;
        break;
    case RT_DEVICE_CTRL_WDT_GET_TIMEOUT:
        result = argument ? LS_OK : LS_INVALID;

        if (argument)
        {
            *(rt_uint32_t *)argument = ls_read(&wdt_io, 8, 4) / 50000000U;
        }

        break;
    case RT_DEVICE_CTRL_WDT_KEEPALIVE:
        result = ls_wdt_feed(&wdt_io);
        break;
    case RT_DEVICE_CTRL_WDT_START:
    case RT_DEVICE_CTRL_WDT_STOP:
        result = ls_wdt_enable(&wdt_io, command == RT_DEVICE_CTRL_WDT_START);
        break;
    default:
        result = LS_UNSUPPORTED;
        break;
    }

    rt_mutex_release(&wdt_lock);
    return ls2k3000_periph_error(result);
}

static rt_err_t wdt_init(rt_watchdog_t *device)
{
    (void)device;
    return RT_EOK;
}

static const struct rt_watchdog_ops wdt_ops = {
    .init = wdt_init,
    .control = wdt_control,
};

static int ls2k3000_wdt_init(void)
{
    int result = ls2k3000_periph_map(0x7a42, 0x50030, 12, &wdt_io);

    if (result)
    {
        return result;
    }

    result = rt_mutex_init(&wdt_lock, "wdt", RT_IPC_FLAG_PRIO);

    if (result)
    {
        return result;
    }

    wdt_device.ops = &wdt_ops;
    result = rt_hw_watchdog_register(&wdt_device, "wdt0", RT_DEVICE_FLAG_RDWR, RT_NULL);

    if (result)
    {
        rt_mutex_detach(&wdt_lock);
    }

    return result;
}
INIT_DEVICE_EXPORT(ls2k3000_wdt_init);
#endif
