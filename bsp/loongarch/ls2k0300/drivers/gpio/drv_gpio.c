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

#include "drv_gpio.h"
#include <rthw.h>
#include "irq.h"

/* 调试日志宏 */
#define DBG_TAG "drv.gpio"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 裸机兼容层 - 寄存器读写函数 */
#ifndef readb
#define readb(addr) (*(volatile rt_uint8_t *)(addr))
#endif
#ifndef writeb
#define writeb(val, addr) (*(volatile rt_uint8_t *)(addr) = (val))
#endif

/* GPIO中断回调函数表 */
static ls2k300_gpio_irq_handler_t gpio_irq_handle_set[LS_GPIO_MAX + 1];
static void *gpio_irq_param_set[LS_GPIO_MAX + 1];

static unsigned short irq_group_map[LS_GPIO_LIO_IRQ_MAP_LEN];

static inline int pin_to_irq_group_index(rt_uint16_t gpio)
{
    return (gpio >> 4);
}

static inline int pin_to_lio_irq_index(rt_uint16_t gpio)
{
    return (gpio >> 4) + LS_GPIO_START_IRQ;
}

static inline int pin_to_rt_irq_index(rt_uint16_t gpio)
{
    return (gpio >> 4) + LS_GPIO_START_IRQ;
}

/*
 * @brief 检查GPIO引脚号是否合法
 */
static inline int check_gpio(rt_uint16_t gpio)
{
    if (gpio > LS_GPIO_MAX)
    {
        LOG_E("Invalid GPIO pin: %d (max: %d)", gpio, LS_GPIO_MAX);
        return -RT_EINVAL;
    }
    return RT_EOK;
}

/*
 * @brief GPIO中断处理函数
 *        扫描同一组GPIO的中断状态，分发处理
 */
static void gpio_irq_handle(int IRQn, void *param)
{
    rt_uint8_t i;
    rt_uint8_t gpio_s = 0;
    rt_uint8_t gpio_e = 0;
    rt_uint8_t temp;
    volatile rt_uint8_t *gpio_reg;

    /* 检查中断号范围 */
    if (IRQn < LS_GPIO_START_IRQ || IRQn > LS_GPIO_END_IRQ)
    {
        return;
    }
    IRQn -= LS_GPIO_START_IRQ;

    gpio_s = IRQn * 16;
    gpio_e = gpio_s + 16;
    if (gpio_e > LS_GPIO_MAX)
    {
        gpio_e = LS_GPIO_MAX + 1;
    }

    /* 扫描该组所有GPIO */
    for (i = gpio_s; i < gpio_e; i++)
    {
        /* 检查该GPIO是否使能中断 */
        gpio_reg = (rt_uint8_t *)LS_GPIO_B_INT_EN_BASE + i;
        temp = readb(gpio_reg);
        if (!temp)
        {
            continue;
        }

        /* 检查该GPIO是否有中断上报 */
        gpio_reg = (rt_uint8_t *)LS_GPIO_B_INT_STS_BASE + i;
        temp = readb(gpio_reg);
        if (!temp)
        {
            continue;
        }

        /* 读取当前GPIO的值 */
        gpio_reg = (rt_uint8_t *)LS_GPIO_B_IN_BASE + i;
        temp = readb(gpio_reg);

        /* 调用回调函数 */
        if (gpio_irq_handle_set[i] != RT_NULL)
        {
            gpio_irq_handle_set[i](gpio_irq_param_set[i]);
        }

        /* 清中断 */
        gpio_reg = (rt_uint8_t *)LS_GPIO_B_INT_CLR_BASE + i;
        writeb(1, gpio_reg);
    }
}

/*
 * @brief 初始化GPIO
 */
rt_err_t ls2k300_gpio_init(struct ls2k300_gpio_init *cfg)
{
    if (cfg == RT_NULL)
    {
        return -RT_EINVAL;
    }

    return ls2k300_gpio_set_dir(cfg->pin, cfg->mode);
}

/*
 * @brief 设置GPIO方向
 * @param pin: GPIO引脚号 (0-63)
 * @param mode: 0=输出, 非0=输入
 */
rt_err_t ls2k300_gpio_set_dir(rt_uint16_t pin, rt_uint8_t mode)
{
    volatile rt_uint8_t *gpio_dir_reg;
    rt_uint8_t dir_mode;

    if (check_gpio(pin) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    /* 转换模式: RT-Thread的PIN_MODE_OUTPUT=0, 但硬件寄存器0=output, 1=input */
    dir_mode = !!(mode);

    /* 设置方向寄存器 */
    gpio_dir_reg = (rt_uint8_t *)LS_GPIO_B_DIR_BASE + pin;
    writeb(dir_mode, gpio_dir_reg);

    LOG_D("GPIO %d set dir: %s", pin, mode ? "INPUT" : "OUTPUT");
    return RT_EOK;
}

/*
 * @brief 获取GPIO方向
 */
rt_err_t ls2k300_gpio_get_dir(rt_uint16_t pin, rt_uint8_t *mode)
{
    volatile rt_uint8_t *gpio_dir_reg;
    rt_uint8_t res;

    if (check_gpio(pin) != RT_EOK || mode == RT_NULL)
    {
        return -RT_EINVAL;
    }

    gpio_dir_reg = (rt_uint8_t *)LS_GPIO_B_DIR_BASE + pin;
    res = readb(gpio_dir_reg);

    /* 转换回RT-Thread模式: 0=output, 非0=input */
    *mode = res ? PIN_MODE_INPUT : PIN_MODE_OUTPUT;

    return RT_EOK;
}

/*
 * @brief 设置GPIO输出值 (自动设置为输出模式)
 */
rt_err_t ls2k300_gpio_write(rt_uint16_t pin, rt_uint8_t value)
{
    volatile rt_uint8_t *gpio_output_reg;

    if (check_gpio(pin) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    /* 先设置为输出模式 */
    if (ls2k300_gpio_set_dir(pin, PIN_MODE_OUTPUT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    /* 设置输出值 */
    gpio_output_reg = (rt_uint8_t *)LS_GPIO_B_OUT_BASE + pin;
    writeb(!!(value), gpio_output_reg);

    LOG_D("GPIO %d write: %d", pin, value);
    return RT_EOK;
}

/*
 * @brief 读取GPIO输入值 (自动设置为输入模式)
 */
rt_err_t ls2k300_gpio_read(rt_uint16_t pin, rt_uint8_t *value)
{
    volatile rt_uint8_t *gpio_input_reg;
    rt_int8_t res;

    if (check_gpio(pin) != RT_EOK || value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* 先设置为输入模式 */
    if (ls2k300_gpio_set_dir(pin, PIN_MODE_INPUT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    /* 读取输入值 */
    gpio_input_reg = (rt_uint8_t *)LS_GPIO_B_IN_BASE + pin;
    res = readb(gpio_input_reg);

    *value = res ? GPIO_HIGH : GPIO_LOW;

    LOG_D("GPIO %d read: %d", pin, *value);
    return RT_EOK;
}

/*
 * @brief 读取GPIO输出寄存器的值 (不改变方向)
 */
rt_err_t ls2k300_gpio_read_output(rt_uint16_t pin, rt_uint8_t *value)
{
    volatile rt_uint8_t *gpio_output_reg;
    rt_int8_t res;

    if (check_gpio(pin) != RT_EOK || value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    gpio_output_reg = (rt_uint8_t *)LS_GPIO_B_OUT_BASE + pin;
    res = readb(gpio_output_reg);

    *value = !!(res);

    return RT_EOK;
}

/*
 * @brief 安装GPIO中断
 * @param cfg: 中断配置参数
 */
rt_err_t ls2k300_gpio_irq_install(struct ls2k300_gpio_irq_cfg *cfg)
{
    volatile rt_uint8_t *gpio_int_en_reg;
    volatile rt_uint8_t *gpio_int_pol_reg;
    volatile rt_uint8_t *gpio_int_edge_reg;
    volatile rt_uint8_t *gpio_int_clr_reg;
    volatile rt_uint8_t *gpio_int_dual_reg;

    unsigned char rt_irq_index;
    char irq_name[16];

    if (cfg == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (check_gpio(cfg->pin) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    if (cfg->trigger < GPIO_IRQ_LOW_TRIGGER || cfg->trigger > GPIO_IRQ_DUAL_EDGE_TRIGGER)
    {
        LOG_E("Invalid trigger mode: %d", cfg->trigger);
        return -RT_EINVAL;
    }

    ls2k300_gpio_set_dir(cfg->pin, GPIO_INPUT_MODE);

    /* 计算寄存器地址 */
    gpio_int_en_reg = (rt_uint8_t *)LS_GPIO_B_INT_EN_BASE + cfg->pin;
    gpio_int_pol_reg = (rt_uint8_t *)LS_GPIO_B_INT_POL_BASE + cfg->pin;
    gpio_int_edge_reg = (rt_uint8_t *)LS_GPIO_B_INT_EDGE_BASE + cfg->pin;
    gpio_int_clr_reg = (rt_uint8_t *)LS_GPIO_B_INT_CLR_BASE + cfg->pin;
    gpio_int_dual_reg = (rt_uint8_t *)LS_GPIO_B_INT_DUAL_BASE + cfg->pin;

    /* 根据触发方式配置寄存器 */
    switch (cfg->trigger)
    {
    case GPIO_IRQ_LOW_TRIGGER:
        writeb(0, gpio_int_pol_reg);
        writeb(0, gpio_int_edge_reg);
        writeb(0, gpio_int_dual_reg);
        break;
    case GPIO_IRQ_HIGH_TRIGGER:
        writeb(1, gpio_int_pol_reg);
        writeb(0, gpio_int_edge_reg);
        writeb(0, gpio_int_dual_reg);
        break;
    case GPIO_IRQ_RISING_EDGE_TRIGGER:
        writeb(1, gpio_int_pol_reg);
        writeb(1, gpio_int_edge_reg);
        writeb(0, gpio_int_dual_reg);
        break;
    case GPIO_IRQ_FALLING_EDGE_TRIGGER:
        writeb(0, gpio_int_pol_reg);
        writeb(1, gpio_int_edge_reg);
        writeb(0, gpio_int_dual_reg);
        break;
    case GPIO_IRQ_DUAL_EDGE_TRIGGER:
        writeb(0, gpio_int_pol_reg);
        writeb(1, gpio_int_edge_reg);
        writeb(1, gpio_int_dual_reg);
        break;
    default:
        return -RT_EINVAL;
    }

    /* 保存回调函数和参数 */
    gpio_irq_handle_set[cfg->pin] = cfg->callback;
    gpio_irq_param_set[cfg->pin] = cfg->param;

    rt_irq_index = pin_to_rt_irq_index(cfg->pin);

    rt_snprintf(irq_name, sizeof(irq_name), "gpio%u", cfg->pin);

    /* 注册到中断控制器 */
    writeb(1, gpio_int_clr_reg);
    writeb(0, gpio_int_en_reg);
    rt_hw_interrupt_install(rt_irq_index, (rt_isr_handler_t)gpio_irq_handle, cfg->param, irq_name);

    LOG_I("GPIO %d IRQ %d installed, trigger: %d", cfg->pin, rt_irq_index, cfg->trigger);
    return RT_EOK;
}

/*
 * @brief 使能/禁用GPIO中断
 */
rt_err_t ls2k300_gpio_irq_enable(rt_uint16_t pin, rt_uint8_t enable)
{
    volatile rt_uint8_t *gpio_int_en_reg;
    volatile rt_uint8_t *gpio_int_clr_reg;

    if (check_gpio(pin) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    gpio_int_en_reg = (rt_uint8_t *)LS_GPIO_B_INT_EN_BASE + pin;
    gpio_int_clr_reg = (rt_uint8_t *)LS_GPIO_B_INT_CLR_BASE + pin;

    writeb(1, gpio_int_clr_reg);
    writeb(!!enable, gpio_int_en_reg);

    if (enable)
    {
        rt_hw_interrupt_umask(pin_to_lio_irq_index(pin));
    }

    LOG_D("GPIO %d IRQ %s", pin, enable ? "enabled" : "disabled");
    return RT_EOK;
}

/* RT-Thread PIN device interface. */

/*
 * @brief RT-Thread PIN设备 - 写入
 */
static void ls2k300_pin_write(rt_device_t dev, rt_base_t pin, rt_uint8_t value)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return;
    }

    ls2k300_gpio_write((rt_uint16_t)pin, (rt_uint8_t)value);
}

/*
 * @brief RT-Thread PIN设备 - 读取
 */
static rt_ssize_t ls2k300_pin_read(rt_device_t dev, rt_base_t pin)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return -RT_EINVAL;
    }

    rt_uint8_t value;
    if (ls2k300_gpio_read((rt_uint16_t)pin, &value) == RT_EOK)
    {
        return value;
    }
    return -1;
}

/*
 * @brief RT-Thread PIN设备 - 设置模式
 */
static void ls2k300_pin_mode(rt_device_t dev, rt_base_t pin, rt_uint8_t mode)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return;
    }

    rt_uint8_t dir_mode;

    switch (mode)
    {
    case PIN_MODE_OUTPUT:
        dir_mode = PIN_MODE_OUTPUT;
        break;
    case PIN_MODE_INPUT:
    case PIN_MODE_INPUT_PULLUP:
    case PIN_MODE_INPUT_PULLDOWN:
        dir_mode = PIN_MODE_INPUT;
        break;
    default:
        LOG_W("Unsupported pin mode: %d", mode);
        return;
    }

    ls2k300_gpio_set_dir((rt_uint16_t)pin, dir_mode);
}

/*
 * @brief RT-Thread PIN设备 - 附加中断回调
 */
static rt_err_t ls2k300_pin_attach_irq(struct rt_device *device, rt_base_t pin, rt_uint8_t mode,
                                       void (*hdr)(void *args), void *args)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return -RT_EINVAL;
    }

    struct ls2k300_gpio_irq_cfg cfg;
    gpio_irq_trigger_enum trigger;

    switch (mode)
    {
    case PIN_IRQ_MODE_RISING:
        trigger = GPIO_IRQ_RISING_EDGE_TRIGGER;
        break;
    case PIN_IRQ_MODE_FALLING:
        trigger = GPIO_IRQ_FALLING_EDGE_TRIGGER;
        break;
    case PIN_IRQ_MODE_RISING_FALLING:
        trigger = GPIO_IRQ_DUAL_EDGE_TRIGGER;
        break;
    case PIN_IRQ_MODE_HIGH_LEVEL:
        trigger = GPIO_IRQ_HIGH_TRIGGER;
        break;
    case PIN_IRQ_MODE_LOW_LEVEL:
        trigger = GPIO_IRQ_LOW_TRIGGER;
        break;
    default:
        return -RT_EINVAL;
    }

    cfg.pin = pin;
    cfg.trigger = trigger;
    cfg.callback = (ls2k300_gpio_irq_handler_t)hdr;
    cfg.param = args;

    return ls2k300_gpio_irq_install(&cfg);
}

/*
 * @brief RT-Thread PIN设备 - 分离中断回调
 */
static rt_err_t ls2k300_pin_detach_irq(struct rt_device *device, rt_base_t pin)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return -RT_EINVAL;
    }

    ls2k300_gpio_irq_enable(pin, 0);
    gpio_irq_handle_set[pin] = RT_NULL;
    gpio_irq_param_set[pin] = RT_NULL;
    return RT_EOK;
}

/*
 * @brief RT-Thread PIN设备 - 使能中断
 */
static rt_err_t ls2k300_pin_irq_enable(struct rt_device *device, rt_base_t pin, rt_uint8_t enabled)
{
    if (pin < 0 || pin > LS_GPIO_MAX)
    {
        return -RT_EINVAL;
    }

    return ls2k300_gpio_irq_enable(pin, (rt_uint8_t)enabled);
}

/* PIN设备操作接口 */
static const struct rt_pin_ops ls2k300_pin_ops = {
    .pin_mode = ls2k300_pin_mode,
    .pin_write = ls2k300_pin_write,
    .pin_read = ls2k300_pin_read,
    .pin_attach_irq = ls2k300_pin_attach_irq,
    .pin_detach_irq = ls2k300_pin_detach_irq,
    .pin_irq_enable = ls2k300_pin_irq_enable,
    .pin_get = RT_NULL,
    .pin_debounce = RT_NULL,
};

/*
 * @brief GPIO驱动初始化
 */
static int ls2k300_gpio_driver_init(void)
{
    rt_err_t err;

    rt_memset(gpio_irq_handle_set, 0, sizeof(gpio_irq_handle_set));
    rt_memset(gpio_irq_param_set, 0, sizeof(gpio_irq_param_set));

    err = rt_device_pin_register("ls2k300-gpio", &ls2k300_pin_ops, RT_NULL);
    if (err != RT_EOK)
    {
        LOG_E("PIN device register failed");
        return -RT_ERROR;
    }

    LOG_I("LS2K300 GPIO driver initialized");
    return RT_EOK;
}

INIT_DEVICE_EXPORT(ls2k300_gpio_driver_init);

/*
 * @brief 硬件GPIO初始化
 */
int rt_hw_gpio_init(void)
{
    return ls2k300_gpio_driver_init();
}
