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
 *   LS2K0300 development BSP: bsp pinmux.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __BSP_PINMUX_H__
#define __BSP_PINMUX_H__

/**
 * @brief LS2K300芯片引脚复用配置头文件
 *
 * 该文件提供LS2K300芯片的引脚复用功能配置宏定义和函数声明，
 * 支持将引脚配置为GPIO模式或不同的外设功能模式。
 * 适用芯片: LS2K300
 * 引脚范围: 0 - 105
 */

/**
 * @brief 引脚模式: 配置为GPIO功能
 */
#define MODE_AS_GPIO 0x0

/**
 * @brief 引脚模式: 配置为第一功能
 */
#define MODE_AS_FIRST_FUNC 0x1

/**
 * @brief 引脚模式: 配置为第二功能
 */
#define MODE_AS_SECOND_FUNC 0x2

/**
 * @brief 引脚模式: 配置为主功能
 */
#define MODE_AS_MAIN_FUNC 0x3

/**
 * @brief 配置指定引脚的复用功能
 *
 * @param pin 引脚编号 (0-105)
 * @param mode 引脚模式
 *             - MODE_AS_GPIO: GPIO模式
 *             - MODE_AS_FIRST_FUNC: 第一功能模式
 *             - MODE_AS_SECOND_FUNC: 第二功能模式
 *             - MODE_AS_MAIN_FUNC: 主功能模式
 * @return 0表示配置成功，非0表示配置失败
 */
int pinmux_pin_setup(int pin, int mode);

#endif
