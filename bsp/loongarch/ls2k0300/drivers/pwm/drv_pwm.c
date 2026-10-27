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
 *   LS2K0300 development BSP: drv pwm.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_pwm.h>
#include <io.h>
#include "loongson_board_info.h"
#include "loongson_soc_info.h"

#define DBG_TAG "drv.pwm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* LS2K300 PWM controller structure - each controller has 1 channel */
struct ls2k300_pwm_controller
{
    struct rt_device_pwm parent;        /* Inherit from RT-Thread PWM device */
    rt_uint64_t reg_base;               /* Controller register base address */
    rt_uint32_t id;                     /* Controller ID (0-3) */
    rt_uint32_t period_ns;              /* Current period in nanoseconds */
    rt_uint8_t polarity;               /* Current polarity */
    rt_bool_t enabled;                /* Enable status */
    char name[8];                /* Device name: "pwm0", "pwm1", etc. */
};

/**
 * @brief Set PWM control register bit
 * @param ctrl: Pointer to controller structure
 * @param bit: Bit position to modify
 * @param val: Value to set (0 or 1)
 */
static void pwm_ctrl_bit_set(struct ls2k300_pwm_controller *ctrl, rt_uint8_t bit, rt_uint8_t val)
{
    rt_uint32_t ctrl_val;

    ctrl_val = readl(ctrl->reg_base + PWM_CTRL);
    ctrl_val |= (1 << bit);                 /* First set bit to 1 */
    ctrl_val ^= (!(val) << bit);            /* XOR with inverted value */
    writel(ctrl_val, ctrl->reg_base + PWM_CTRL);
    ctrl_val = readl(ctrl->reg_base + PWM_CTRL);
}

/**
 * @brief Enable PWM counting
 * @param ctrl: Pointer to controller structure
 */
static void pwm_enable_count(struct ls2k300_pwm_controller *ctrl)
{
    pwm_ctrl_bit_set(ctrl, PWM_EN, 1);
}

/**
 * @brief Disable PWM counting
 * @param ctrl: Pointer to controller structure
 */
static void pwm_disable_count(struct ls2k300_pwm_controller *ctrl)
{
    pwm_ctrl_bit_set(ctrl, PWM_EN, 0);
}

/**
 * @brief Enable PWM output
 * @param ctrl: Pointer to controller structure
 */
static void pwm_output_enable(struct ls2k300_pwm_controller *ctrl)
{
    pwm_ctrl_bit_set(ctrl, PWM_OE, 0);
}

/**
 * @brief Disable PWM output
 * @param ctrl: Pointer to controller structure
 */
static void pwm_output_disable(struct ls2k300_pwm_controller *ctrl)
{
    pwm_ctrl_bit_set(ctrl, PWM_OE, 1);
}

/**
 * @brief Disable PWM and force output to inactive level
 * @param ctrl: Pointer to controller structure
 * @note When disabled, output is forced to inactive level based on polarity
 */
static void pwm_force_inactive(struct ls2k300_pwm_controller *ctrl)
{
    rt_uint32_t ori_period, ori_duty;
    rt_uint32_t ctrl_val;

    /* Save original values */
    ori_duty = readl(ctrl->reg_base + PWM_LOW_BUFFER);
    ori_period = readl(ctrl->reg_base + PWM_FULL_BUFFER);
    ctrl_val = readl(ctrl->reg_base + PWM_CTRL);

    /* Disable counting */
    ctrl_val |= (1 << PWM_EN);
    ctrl_val ^= (1 << PWM_EN);
    writel(ctrl_val, ctrl->reg_base + PWM_CTRL);

    /*
     * To ensure output goes to inactive level when stopped:
     * Set period=1, duty=0, so counter stops at inactive level
     */
    writel(1, ctrl->reg_base + PWM_FULL_BUFFER);
    writel(0, ctrl->reg_base + PWM_LOW_BUFFER);
    writel(ctrl_val, ctrl->reg_base + PWM_CTRL);

    /* Restore original values for next enable */
    writel(ori_period, ctrl->reg_base + PWM_FULL_BUFFER);
    writel(ori_duty, ctrl->reg_base + PWM_LOW_BUFFER);
}

/**
 * @brief Convert nanoseconds to PWM counter value
 * @param ns: Time in nanoseconds
 * @return Counter value
 */
static rt_uint32_t pwm_ns_to_count(rt_uint32_t ns)
{
    return (rt_uint32_t)((rt_uint64_t)PWM_REF_CLK * ns / SECOND_TO_NS);
}

/**
 * @brief Update PWM period and duty cycle
 * @param ctrl: Pointer to controller structure
 * @param period_ns: PWM period in nanoseconds
 * @param pulse_ns: PWM pulse width (active level duration) in nanoseconds
 * @return RT_EOK on success, -RT_ERROR on failure
 */
static rt_err_t pwm_period_pulse_update(struct ls2k300_pwm_controller *ctrl, rt_uint32_t period_ns,
                                        rt_uint32_t pulse_ns)
{
    rt_uint32_t period_cnt, pulse_cnt;

    if (pulse_ns > period_ns)
    {
        LOG_E("Pulse width (%d ns) cannot exceed period (%d ns)", pulse_ns, period_ns);
        return -RT_EINVAL;
    }

    /* Convert nanoseconds to counter values */
    period_cnt = pwm_ns_to_count(period_ns);
    pulse_cnt = pwm_ns_to_count(pulse_ns);

    if (period_cnt == 0)
    {
        LOG_W("Period too small for PWM ref clock %d Hz, using minimum", PWM_REF_CLK);
        period_cnt = 1;
    }

    if (pulse_cnt == 0 && pulse_ns > 0)
    {
        pulse_cnt = 1;
    }

    if (pulse_cnt >= period_cnt)
    {
        pulse_cnt = period_cnt - 1;
    }

    LOG_D("%s: period=%d ns (%d cnt), pulse=%d ns (%d cnt)", ctrl->name, period_ns, period_cnt,
          pulse_ns, pulse_cnt);

    /* Disable counting before updating registers */
    pwm_disable_count(ctrl);

    /*
     * Set period and duty:
     * - FULL_BUFFER: total period count
     * - LOW_BUFFER: active level count (pulse width)
     */
    writel(1, ctrl->reg_base + PWM_LOW_BUFFER);
    writel(period_cnt, ctrl->reg_base + PWM_FULL_BUFFER);
    writel(pulse_cnt, ctrl->reg_base + PWM_LOW_BUFFER);

    /* Re-enable if previously enabled */
    if (ctrl->enabled)
    {
        pwm_enable_count(ctrl);
    }

    ctrl->period_ns = period_ns;

    return RT_EOK;
}

/**
 * @brief Update PWM polarity
 * @param ctrl: Pointer to controller structure
 * @param polarity: PWM_POLARITY_NORMAL or PWM_POLARITY_INVERT
 */
static void pwm_polarity_update(struct ls2k300_pwm_controller *ctrl, rt_uint8_t polarity)
{
    pwm_disable_count(ctrl);

    /*
     * Note: Hardware PWM_INVERT bit meaning is opposite to software definition
     * PWM_POLARITY_NORMAL (0) -> Invert bit = 1
     * PWM_POLARITY_INVERT (1) -> Invert bit = 0
     */
    pwm_ctrl_bit_set(ctrl, PWM_INVERT, !polarity);

    ctrl->polarity = polarity;

    if (ctrl->enabled)
    {
        pwm_enable_count(ctrl);
    }
}

/**
 * @brief RT-Thread PWM control interface
 */
static rt_err_t drv_pwm_control(struct rt_device_pwm *device, int cmd, void *arg)
{
    struct ls2k300_pwm_controller *ctrl;
    struct rt_pwm_configuration *cfg;
    rt_err_t result = RT_EOK;

    RT_ASSERT(device != RT_NULL);

    ctrl = (struct ls2k300_pwm_controller *)device;
    cfg = (struct rt_pwm_configuration *)arg;

    switch (cmd)
    {
    case PWM_CMD_ENABLE:
        pwm_enable_count(ctrl);
        pwm_output_enable(ctrl);
        ctrl->enabled = RT_TRUE;
        LOG_D("%s enabled", ctrl->name);
        break;

    case PWM_CMD_DISABLE:
        pwm_force_inactive(ctrl);
        pwm_output_disable(ctrl);
        ctrl->enabled = RT_FALSE;
        LOG_D("%s disabled", ctrl->name);
        break;

    case PWM_CMD_SET:
        /* Set period and pulse width */
        result = pwm_period_pulse_update(ctrl, cfg->period, cfg->pulse);
        break;

    case PWM_CMD_SET_PERIOD:
        /* Set period only, keep current duty ratio */
        {
            rt_uint32_t current_pulse;

            /* Read current pulse count and convert to ns */
            current_pulse = readl(ctrl->reg_base + PWM_LOW_BUFFER);
            current_pulse = (rt_uint32_t)((rt_uint64_t)current_pulse * SECOND_TO_NS / PWM_REF_CLK);

            if (ctrl->period_ns > 0)
            {
                /* Maintain duty ratio */
                rt_uint32_t new_pulse = (rt_uint32_t)((rt_uint64_t)cfg->period * current_pulse /
                                                      ctrl->period_ns);
                result = pwm_period_pulse_update(ctrl, cfg->period, new_pulse);
            }
            else
            {
                /* Default 50% duty if period not set before */
                result = pwm_period_pulse_update(ctrl, cfg->period, cfg->period / 2);
            }
        }
        break;

    case PWM_CMD_SET_PULSE:
        /* Set pulse width only, keep current period */
        result = pwm_period_pulse_update(ctrl, ctrl->period_ns, cfg->pulse);
        break;

    case PWM_CMD_GET:
        /* Get current configuration */
        {
            rt_uint32_t period_cnt = readl(ctrl->reg_base + PWM_FULL_BUFFER);
            rt_uint32_t pulse_cnt = readl(ctrl->reg_base + PWM_LOW_BUFFER);

            cfg->period = (rt_uint32_t)((rt_uint64_t)period_cnt * SECOND_TO_NS / PWM_REF_CLK);
            cfg->pulse = (rt_uint32_t)((rt_uint64_t)pulse_cnt * SECOND_TO_NS / PWM_REF_CLK);
        }
        break;

    case PWM_CMD_SET_PHASE:
        /* Set polarity */
        if (cfg->phase == PWM_POLARITY_NORMAL || cfg->phase == PWM_POLARITY_INVERT)
        {
            pwm_polarity_update(ctrl, cfg->phase);
        }
        else
        {
            result = -RT_EINVAL;
        }
        break;

    default:
        LOG_W("%s: Unsupported PWM command: %d", ctrl->name, cmd);
        result = -RT_EINVAL;
        break;
    }

    return result;
}

static struct rt_pwm_ops drv_pwm_ops = { .control = drv_pwm_control };

/* Array of 4 independent PWM controllers */
static struct ls2k300_pwm_controller _pwm_controllers[LS2K300_PWM_CONTROLLER_NUM];

/**
 * @brief Initialize single PWM controller
 * @param id: Controller ID (0-3)
 * @param reg_base: Register base address
 * @return RT_EOK on success
 */
static rt_err_t ls2k300_pwm_controller_init(rt_uint32_t id, rt_uint64_t reg_base)
{
    struct ls2k300_pwm_controller *ctrl = &_pwm_controllers[id];
    rt_err_t result;

    /* Initialize controller structure */
    ctrl->reg_base = reg_base;
    ctrl->id = id;
    ctrl->period_ns = 0;
    ctrl->polarity = PWM_POLARITY_NORMAL;
    ctrl->enabled = RT_FALSE;
    rt_snprintf(ctrl->name, sizeof(ctrl->name), "pwm%d", id);

    /* Disable counting and output */
    writel(0, ctrl->reg_base + PWM_CTRL);

    /* Set default period (1ms) and duty (0%) */
    writel(1000, ctrl->reg_base + PWM_FULL_BUFFER);
    writel(0, ctrl->reg_base + PWM_LOW_BUFFER);
    ctrl->period_ns = 1000000;  /* 1ms */

    /* Register as independent PWM device */
    result = rt_device_pwm_register(&ctrl->parent, ctrl->name, &drv_pwm_ops, RT_NULL);
    if (result != RT_EOK)
    {
        LOG_E("%s register failed: %d", ctrl->name, result);
        return result;
    }

    LOG_D("%s initialized at 0x%08X", ctrl->name, reg_base);
    return RT_EOK;
}

/**
 * @brief Initialize all LS2K300 PWM controllers
 */
int rt_hw_pwm_init(void)
{
    rt_err_t result;

    /* Initialize 4 independent PWM controllers */
    result = ls2k300_pwm_controller_init(0, LS_PWM0_REG_BASE);
    if (result != RT_EOK)
    {
        return result;
    }
    LOG_I("pwm0 registered successfully");

    result = ls2k300_pwm_controller_init(1, LS_PWM1_REG_BASE);
    if (result != RT_EOK)
    {
        return result;
    }
    LOG_I("pwm1 registered successfully");

    result = ls2k300_pwm_controller_init(2, LS_PWM2_REG_BASE);
    if (result != RT_EOK)
    {
        return result;
    }
    LOG_I("pwm2 registered successfully");

    result = ls2k300_pwm_controller_init(3, LS_PWM3_REG_BASE);
    if (result != RT_EOK)
    {
        return result;
    }
    LOG_I("pwm3 registered successfully");

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_pwm_init);
