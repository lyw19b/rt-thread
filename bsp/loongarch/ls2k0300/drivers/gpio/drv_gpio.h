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
 *   LS2K0300 development BSP: drv gpio.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_GPIO_H__
#define __DRV_GPIO_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "loongson_soc_info.h"
#include "loongson_board_info.h"

/* GPIO 数量和中断配置 */
#define LS_GPIO_IRQ_SHARED_INTERVAL 16      /* 每16个GPIO共享一个中断 */

/* 传统中断号范围 (中断号53-59) */
#define LS_GPIO_START_IRQ 53
#define LS_GPIO_END_IRQ   59

/* GPIO 模式定义 */
#define GPIO_OUTPUT_MODE 0       /* GPIO_OEN 0 代表输出 */
#define GPIO_INPUT_MODE  1       /* GPIO_OEN 1 代表输入 */

/* GPIO 电平定义 */
#define GPIO_HIGH  1
#define GPIO_LOW   0
#define GPIO_ERROR (-1)

/* 中断触发方式 */
typedef enum
{
    GPIO_IRQ_LOW_TRIGGER = 0,
    GPIO_IRQ_HIGH_TRIGGER,
    GPIO_IRQ_RISING_EDGE_TRIGGER,
    GPIO_IRQ_FALLING_EDGE_TRIGGER,
    GPIO_IRQ_DUAL_EDGE_TRIGGER,
} gpio_irq_trigger_enum;

/* GPIO 初始化结构体 */
struct ls2k300_gpio_init
{
    rt_uint16_t pin;
    rt_uint16_t mode;       /* PIN_MODE_OUTPUT, PIN_MODE_INPUT等 */
};

/* GPIO 中断回调函数类型 */
typedef void (*ls2k300_gpio_irq_handler_t)(void *param);

/* 中断配置结构体 */
struct ls2k300_gpio_irq_cfg
{
    rt_uint16_t pin;
    gpio_irq_trigger_enum trigger;
    ls2k300_gpio_irq_handler_t callback;
    void *param;
};

/* 驱动接口函数声明 */
int rt_hw_gpio_init(void);
rt_err_t ls2k300_gpio_init(struct ls2k300_gpio_init *cfg);
rt_err_t ls2k300_gpio_set_dir(rt_uint16_t pin, rt_uint8_t mode);
rt_err_t ls2k300_gpio_get_dir(rt_uint16_t pin, rt_uint8_t *mode);
rt_err_t ls2k300_gpio_write(rt_uint16_t pin, rt_uint8_t value);
rt_err_t ls2k300_gpio_read(rt_uint16_t pin, rt_uint8_t *value);
rt_err_t ls2k300_gpio_read_output(rt_uint16_t pin, rt_uint8_t *value);
rt_err_t ls2k300_gpio_irq_install(struct ls2k300_gpio_irq_cfg *cfg);
rt_err_t ls2k300_gpio_irq_enable(rt_uint16_t pin, rt_uint8_t enable);

#endif /* __DRV_GPIO_H__ */
