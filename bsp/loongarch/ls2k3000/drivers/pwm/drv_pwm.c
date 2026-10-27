/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_pwm.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Register four PWM channels with serialized configuration and explicit enable.
 */

#include <rtdevice.h>
#include "periph_resource.h"
#include "pwm_hw.h"

static struct ls_periph_io pwm_io;
static struct rt_mutex pwm_lock;
static struct rt_device_pwm pwm_device;

static rt_err_t pwm_control(struct rt_device_pwm *device, int command, void *argument)
{
    struct rt_pwm_configuration *config = argument;
    int result;

    (void)device;

    if (!config || config->channel >= 4 || config->complementary || rt_interrupt_get_nest())
    {
        return -RT_EINVAL;
    }

    result = rt_mutex_take(&pwm_lock, RT_WAITING_FOREVER);

    if (result)
    {
        return result;
    }

    switch (command)
    {
    case PWM_CMD_SET:
        result = ls_pwm_set(&pwm_io, config->channel, config->period, config->pulse);
        break;
    case PWM_CMD_GET:
        result = ls_pwm_get(&pwm_io, config->channel, &config->period, &config->pulse);
        break;
    case PWM_CMD_ENABLE:
    case PWM_CMD_DISABLE:
        result = ls_pwm_enable(&pwm_io, config->channel, command == PWM_CMD_ENABLE);
        break;
    default:
        result = LS_UNSUPPORTED;
        break;
    }

    rt_mutex_release(&pwm_lock);
    return ls2k3000_periph_error(result);
}

static const struct rt_pwm_ops pwm_ops = {
    .control = pwm_control,
};

static int ls2k3000_pwm_init(void)
{
    int result = ls2k3000_periph_map(0x7a42, 0x20000, 0x400, &pwm_io);

    if (result)
    {
        return result;
    }

    result = rt_mutex_init(&pwm_lock, "pwm", RT_IPC_FLAG_PRIO);

    if (result)
    {
        return result;
    }

    result = rt_device_pwm_register(&pwm_device, "pwm0", &pwm_ops, RT_NULL);

    if (result)
    {
        rt_mutex_detach(&pwm_lock);
    }

    return result;
}
INIT_DEVICE_EXPORT(ls2k3000_pwm_init);
