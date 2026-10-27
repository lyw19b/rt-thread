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
 *   LS2K0300 development BSP: bsp thermal.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __BSP_THERMAL_H__
#define __BSP_THERMAL_H__

#include <rtthread.h>
#include <ls2k300.h>

/**
 * @brief 获取芯片温度
 *
 * @return 温度值（单位：毫摄氏度 m°C），失败返回 RT_NULL
 */
int ls2k300_thermal_get_temp(void);

#endif /* __BSP_THERMAL_H__ */
