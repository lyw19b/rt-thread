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

#ifndef __DRV_PWM_H__
#define __DRV_PWM_H__

#include <rtthread.h>
#include <rtdevice.h>

/* LS2K300 has 4 independent PWM controllers, each with 1 channel */
#define LS2K300_PWM_CONTROLLER_NUM 4

/* PWM controller IDs */
#define LS2K300_PWM0 0
#define LS2K300_PWM1 1
#define LS2K300_PWM2 2
#define LS2K300_PWM3 3

//输出翻转使能
enum
{
    PWM_POLARITY_NORMAL = 0, // 高电平为有效电平，周期开始时，高电平开始
    PWM_POLARITY_INVERT      // 低电平为有效电平，周期开始时，低电平开始
};

int rt_hw_pwm_init(void);

#endif /* __DRV_PWM_H__ */
