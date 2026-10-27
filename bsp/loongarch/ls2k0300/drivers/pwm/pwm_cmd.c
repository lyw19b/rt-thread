/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: pwm cmd.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <drv_pwm.h>
#include "loongson_soc_info.h"
#include "loongson_board_info.h"

#ifdef RT_USING_FINSH

#include <finsh.h>

#define DBG_TAG "drv.pwm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/**
 * @brief PWM control command for Finsh/MSH
 * @param argc: argument count
 * @param argv: argument vector
 *        argv[1]: PWM controller ID (0-3)
 *        argv[2]: Period in nanoseconds
 *        argv[3]: Duty cycle in percent (0-100)
 *        argv[4]: Polarity (0=normal, 1=inverted), optional, default=0
 * @return 0 on success, -1 on failure
 */
static int pwm_set_cmd(int argc, char *argv[])
{
    struct rt_device_pwm *pwm_dev;
    rt_uint32_t id;
    rt_uint32_t period_ns;
    rt_uint32_t duty_percent;
    rt_uint32_t pulse_ns;
    rt_uint8_t polarity = PWM_POLARITY_NORMAL;  /* Default: normal polarity */
    char dev_name[8];
    rt_err_t result;

    /* Check argument count */
    if (argc < 4)
    {
        rt_kprintf("Usage: pwm_set <id> <period_ns> <duty_cycle_percent> [polarity]\n");
        rt_kprintf("  id: PWM controller number (0-3)\n");
        rt_kprintf("  period_ns: PWM period in nanoseconds\n");
        rt_kprintf("  duty_cycle_percent: Duty cycle in percent (0-100)\n");
        rt_kprintf("  polarity: 0=normal (high active), 1=inverted (low active), default=0\n");
        rt_kprintf("Example: pwm_set 0 1000000 50 0\n");
        rt_kprintf("         (PWM0, 1ms period, 50%% duty, normal polarity)\n");
        return -1;
    }

    /* Parse PWM ID */
    id = atoi(argv[1]);
    if (id >= LS2K300_PWM_CONTROLLER_NUM)
    {
        LOG_E("Invalid PWM ID: %d (valid range: 0-%d)", id, LS2K300_PWM_CONTROLLER_NUM - 1);
        return -1;
    }

    /* Parse period */
    period_ns = atoi(argv[2]);
    if (period_ns == 0)
    {
        LOG_E("Invalid period: %s", argv[2]);
        return -1;
    }

    /* Parse duty cycle */
    duty_percent = atoi(argv[3]);
    if (duty_percent > 100)
    {
        LOG_E("Invalid duty cycle: %d%% (valid range: 0-100)", duty_percent);
        return -1;
    }

    polarity = atoi(argv[4]);
    if (polarity != PWM_POLARITY_NORMAL && polarity != PWM_POLARITY_INVERT)
    {
        LOG_E("Invalid polarity: %d (valid: 0=normal, 1=inverted)", polarity);
        return -1;
    }

    /* Calculate pulse width in nanoseconds */
    pulse_ns = (rt_uint32_t)((rt_uint64_t)period_ns * duty_percent / 100);

    /* Get PWM device */
    rt_snprintf(dev_name, sizeof(dev_name), "pwm%d", id);
    pwm_dev = (struct rt_device_pwm *)rt_device_find(dev_name);
    if (pwm_dev == RT_NULL)
    {
        LOG_E("PWM device %s not found", dev_name);
        return -1;
    }

    /* Disable PWM before configuration */
    rt_pwm_disable(pwm_dev, 0);

    /* Set polarity */
    result = rt_pwm_set_phase(pwm_dev, 0, polarity);
    if (result != RT_EOK)
    {
        LOG_E("Failed to set %s polarity", dev_name);
        return -1;
    }

    /* Set period and duty cycle */
    result = rt_pwm_set(pwm_dev, 0, period_ns, pulse_ns);
    if (result != RT_EOK)
    {
        LOG_E("Failed to configure %s", dev_name);
        return -1;
    }

    /* Enable PWM */
    result = rt_pwm_enable(pwm_dev, 0);
    if (result != RT_EOK)
    {
        LOG_E("Failed to enable %s", dev_name);
        return -1;
    }

    /* Print configuration summary */
    rt_kprintf("%s configured: period=%d ns (%d Hz), duty=%d%% (%d ns), polarity=%s\n", dev_name,
               period_ns, SECOND_TO_NS / period_ns, duty_percent, pulse_ns,
               polarity == PWM_POLARITY_NORMAL ? "normal" : "inverted");

    return 0;
}

MSH_CMD_EXPORT(pwm_set_cmd, Set LS2K300 PWM parameters : pwm_set<id><period><duty>[polarity]);

#endif
