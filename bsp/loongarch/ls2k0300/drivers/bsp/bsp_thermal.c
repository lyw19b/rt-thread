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

#include "bsp_thermal.h"
#include <io.h>
#include <rtdevice.h>

/* Temperature calculation constants */
#define TEMP_COEFFICIENT 570
#define TEMP_OFFSET      394700

/* Register read/write macros */
#define thermal_readl(addr)       readl((addr))
#define thermal_writel(val, addr) writel((val), (addr))

static rt_bool_t thermal_inited = RT_FALSE;
static int val_fix;

/**
 * @brief 初始化温度传感器
 */
void ls2k300_thermal_init(void)
{
    int version;
    if (thermal_inited)
    {
        return;
    }

    /* 配置温度传感器 */
    writel(0xff03, LS_THERMAL_REG_BASE + LOONGSON2_THSENS_CFG_REG);

    /* 读取版本信息 */
    version = thermal_readl(LS_THERMAL_REG_BASE + 0x2AF0);
    version >>= 4;
    version &= 1;

    /* 根据版本读取修正值 */
    if (version)
    {
        val_fix = thermal_readl(LS_THERMAL_REG_BASE + 0x2AF4) & 0xffff;
    }
    else
    {
        val_fix = (thermal_readl(LS_THERMAL_REG_BASE + 0x2AF0) >> 20) & 0xffff;
    }

    /* 处理有符号修正值 */
    if (val_fix & 0x8000)
    {
        val_fix = -(val_fix & 0x7fff);
    }
    else
    {
        val_fix = val_fix & 0x7fff;
    }

    thermal_inited = RT_TRUE;
    rt_kprintf("LS2K300 thermal sensor initialized\n");
}

/**
 * @brief 读取 LS2K300 芯片温度传感器的温度值
 *
 * @return 温度值（单位：毫摄氏度 m°C），失败返回 -RT_ERROR
 */
int ls2k300_thermal_get_temp(void)
{
    int temp_val;
    unsigned int reg_val;

    /* 自动初始化 */
    if (!thermal_inited)
    {
        ls2k300_thermal_init();
    }

    /* 读取温度传感器输出值 */
    reg_val = thermal_readl(LS_THERMAL_REG_BASE + LOONGSON2_THSENS_OUT_REG);
    reg_val &= 0x7ff;

    /* 计算温度值 */
    temp_val = ((int)reg_val + val_fix) * TEMP_COEFFICIENT - TEMP_OFFSET;

    return temp_val;
}

/* 自动初始化 */
static int rt_hw_thermal_init(void)
{
    ls2k300_thermal_init();
    return 0;
}

INIT_DEVICE_EXPORT(rt_hw_thermal_init);

/* 命令行接口：读取温度 */
#ifdef RT_USING_FINSH
#include <finsh.h>

static int thermal(int argc, char **argv)
{
    int temp;

    temp = ls2k300_thermal_get_temp();
    if (temp > -274000000)  /* 高于绝对零度视为有效 */
    {
        rt_kprintf("Current temperature: %d.%03d ℃\n", temp / 1000, temp % 1000);
    }
    else
    {
        rt_kprintf("Failed to read temperature\n");
    }

    return 0;
}

MSH_CMD_EXPORT(thermal, Read LS2K300 chip temperature);

#endif /* RT_USING_FINSH */
