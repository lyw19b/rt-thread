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
 *   LS2K0300 development BSP: drv adc.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_adc.h>

#include <stdlib.h>

#define DBG_TAG "drv.adc"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* LS2K300 ADC 设备实例 */
static struct ls2k300_adc_device _ls2k300_adc;
static struct rt_mutex adc_lock;

/**
 * @brief 写 ADC 寄存器
 */
static inline void adc_writel(struct ls2k300_adc_device *adc, rt_uint32_t offset, rt_uint32_t val)
{
    writel(val, adc->reg_base + offset);
}

/**
 * @brief 读 ADC 寄存器
 */
static inline rt_uint32_t adc_readl(struct ls2k300_adc_device *adc, rt_uint32_t offset)
{
    return readl(adc->reg_base + offset);
}

/**
 * @brief 等待校准完成
 */
static rt_err_t adc_wait_calibration(struct ls2k300_adc_device *adc)
{
    rt_uint32_t cr2;
    rt_tick_t start = rt_tick_get();

    do
    {
        cr2 = adc_readl(adc, ADC_CR2_OFFSET);
        if (!(cr2 & (1 << ADC_CR2_CAL_SHIFT)))
        {
            return RT_EOK;
        }
    } while ((rt_tick_get() - start) < rt_tick_from_millisecond(100));

    LOG_E("ADC calibration timeout");
    return -RT_ETIMEOUT;
}

/**
 * @brief 等待转换结束
 */
static rt_err_t adc_wait_eoc(struct ls2k300_adc_device *adc, rt_tick_t timeout_ms)
{
    rt_tick_t start = rt_tick_get();

    do
    {
        if (adc_readl(adc, ADC_SR_OFFSET) & ADC_SR_EOC)
        {
            return RT_EOK;
        }
    } while ((rt_tick_get() - start) < rt_tick_from_millisecond(timeout_ms));

    LOG_E("ADC conversion timeout");
    return -RT_ETIMEOUT;
}

/**
 * @brief 设置 ADC 通道采样时间
 */
static void adc_set_sample_time(struct ls2k300_adc_device *adc, rt_uint8_t channel,
                                rt_uint8_t sample_time)
{
    rt_uint32_t val;

    if (channel > ADC_CHANNEL_MAX)
    {
        LOG_W("Invalid ADC channel: %d", channel);
        return;
    }

    /* All eight channels use SMPR2. */
    val = adc_readl(adc, ADC_SMPR2_OFFSET);
    val &= ~(0x7U << (3 * channel));
    val |= (sample_time & 0x7U) << (3 * channel);
    adc_writel(adc, ADC_SMPR2_OFFSET, val);
}

/**
 * @brief 设置 ADC 规则通道
 * @param adc: ADC 设备
 * @param channel: ADC 通道号 (0-7)
 * @param seq_num: 序列号 (1-16)
 */
static void adc_set_regular_channel(struct ls2k300_adc_device *adc, rt_uint8_t channel,
                                    rt_uint8_t seq_num)
{
    rt_uint32_t val;
    rt_uint32_t offset;
    rt_uint32_t shift;

    if (channel > ADC_CHANNEL_MAX || seq_num < 1 || seq_num > 16)
    {
        LOG_W("Invalid parameters: channel=%d, seq_num=%d", channel, seq_num);
        return;
    }

    /* 根据序列号确定寄存器和偏移 */
    if (seq_num < 7)
    {
        /* SQR3: 序列 1-6 */
        offset = ADC_SQR3_OFFSET;
        shift = 5 * (seq_num - 1);
    }
    else if (seq_num < 13)
    {
        /* SQR2: 序列 7-12 */
        offset = ADC_SQR2_OFFSET;
        shift = 5 * (seq_num - 7);
    }
    else
    {
        /* SQR1: 序列 13-16 */
        offset = ADC_SQR1_OFFSET;
        shift = 5 * (seq_num - 13);
    }

    val = adc_readl(adc, offset);
    val &= ~(0x1F << shift);
    val |= (channel & 0x1F) << shift;
    adc_writel(adc, offset, val);

    /* 设置序列长度为 1 次转换 (SQR1 中 L[3:0] = 0) */
    if (seq_num == 1)
    {
        val = adc_readl(adc, ADC_SQR1_OFFSET);
        val &= ~(0xF << 20);  /* 清除 L[3:0] 位 */
        adc_writel(adc, ADC_SQR1_OFFSET, val);
    }
}

/**
 * @brief 初始化 ADC 硬件
 */
static rt_err_t ls2k300_adc_hw_init(struct ls2k300_adc_device *adc)
{
    rt_uint32_t cr1, cr2;

    if (adc->initialized)
    {
        return RT_EOK;
    }

    /* 设置 ADC 时钟分频 (PCLK/2) */
    cr1 = (2 << ADC_CR1_CLKDIV_SHIFT);
    adc_writel(adc, ADC_CR1_OFFSET, cr1);

    /* ADC 上电 */
    cr2 = (1 << ADC_CR2_ADON_SHIFT);
    adc_writel(adc, ADC_CR2_OFFSET, cr2);

    /* 配置外部触发和软件启动 */
    cr2 |= (0x7 << ADC_CR2_EXTSEL_SHIFT) |   /* SWSTART 作为外部触发 */
           (1 << ADC_CR2_EXTTRIG_SHIFT);      /* 使能外部触发 */
    adc_writel(adc, ADC_CR2_OFFSET, cr2);

    /* 复位校准 */
    cr2 |= (1 << ADC_CR2_RSTCAL_SHIFT);
    adc_writel(adc, ADC_CR2_OFFSET, cr2);

    /* 启动校准 */
    cr2 = adc_readl(adc, ADC_CR2_OFFSET);
    cr2 |= (1 << ADC_CR2_CAL_SHIFT);
    adc_writel(adc, ADC_CR2_OFFSET, cr2);

    /* 等待校准完成 */
    if (adc_wait_calibration(adc) != RT_EOK)
    {
        LOG_E("ADC calibration failed");
        return -RT_ERROR;
    }

    LOG_D("ADC hardware initialized successfully");
    adc->initialized = RT_TRUE;

    return RT_EOK;
}

/**
 * @brief 使能/禁用 ADC 通道
 */
static rt_err_t ls2k300_adc_enabled(struct rt_adc_device *device, rt_int8_t channel,
                                    rt_bool_t enabled)
{
    struct ls2k300_adc_device *adc = (struct ls2k300_adc_device *)device;

    if (adc == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (channel < 0 || channel > ADC_CHANNEL_MAX)
    {
        LOG_E("Invalid channel: %d", channel);
        return -RT_EINVAL;
    }

    rt_mutex_take(&adc_lock, RT_WAITING_FOREVER);

    if (enabled)
    {
        /* 设置该通道的采样时间 */
        adc_set_sample_time(adc, channel, ADC_DEFAULT_SAMPLETIME);

        /* 在规则序列中配置通道 (位置 1) */
        adc_set_regular_channel(adc, channel, 1);

        LOG_D("ADC channel %d enabled", channel);
    }
    else
    {
        /* ADC 通道无法单独真正禁用,
         * 但可以设置很长的采样时间来降低功耗 */
        adc_set_sample_time(adc, channel, ADC_SAMPLETIME_128CYCLES);
        LOG_D("ADC channel %d disabled", channel);
    }

    rt_mutex_release(&adc_lock);
    return RT_EOK;
}

/**
 * @brief 执行 ADC 转换
 */
static rt_err_t ls2k300_adc_convert(struct rt_adc_device *device, rt_int8_t channel,
                                    rt_uint32_t *value)
{
    struct ls2k300_adc_device *adc = (struct ls2k300_adc_device *)device;
    rt_uint32_t cr2;
    rt_err_t result;

    if (adc == RT_NULL || value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (channel < 0 || channel > ADC_CHANNEL_MAX)
    {
        LOG_E("Invalid channel: %d", channel);
        return -RT_EINVAL;
    }

    rt_mutex_take(&adc_lock, RT_WAITING_FOREVER);

    /* 确保 ADC 已初始化 */
    if (!adc->initialized)
    {
        result = ls2k300_adc_hw_init(adc);
        if (result != RT_EOK)
        {
            rt_mutex_release(&adc_lock);
            return result;
        }
    }

    /* 设置采样时间和通道 */
    adc_set_sample_time(adc, channel, ADC_DEFAULT_SAMPLETIME);
    adc_set_regular_channel(adc, channel, 1);

    /* 清除 EOC 标志 */
    adc_readl(adc, ADC_DR_OFFSET);

    /* 启动转换 */
    cr2 = adc_readl(adc, ADC_CR2_OFFSET);
    cr2 |= (1 << ADC_CR2_SWSTART_SHIFT);
    adc_writel(adc, ADC_CR2_OFFSET, cr2);

    /* 等待转换完成 */
    result = adc_wait_eoc(adc, 100);
    if (result != RT_EOK)
    {
        rt_mutex_release(&adc_lock);
        return result;
    }

    /* 读取转换值 */
    *value = adc_readl(adc, ADC_DR_OFFSET) & 0xFFF;

    LOG_D("ADC channel %d converted value: %d", channel, *value);

    rt_mutex_release(&adc_lock);
    return RT_EOK;
}

/**
 * @brief 获取 ADC 分辨率
 */
static rt_uint8_t ls2k300_adc_get_resolution(struct rt_adc_device *device)
{
    struct ls2k300_adc_device *adc = (struct ls2k300_adc_device *)device;

    if (adc == RT_NULL)
    {
        return 0;
    }

    return adc->resolution;
}

/**
 * @brief 获取 ADC 参考电压
 */
static rt_int16_t ls2k300_adc_get_vref(struct rt_adc_device *device)
{
    struct ls2k300_adc_device *adc = (struct ls2k300_adc_device *)device;

    if (adc == RT_NULL)
    {
        return -1;
    }

    return adc->vref_mv;
}

/* ADC 操作接口 */
static const struct rt_adc_ops ls2k300_adc_ops = {
    .enabled = ls2k300_adc_enabled,
    .convert = ls2k300_adc_convert,
    .get_resolution = ls2k300_adc_get_resolution,
    .get_vref = ls2k300_adc_get_vref,
};

/**
 * @brief 初始化 LS2K300 ADC 驱动
 */
int rt_hw_adc_init(void)
{
    rt_err_t result;

    /* 初始化 ADC 设备结构体 */
    _ls2k300_adc.reg_base = LS_ADC_REG_BASE;
    _ls2k300_adc.resolution = ADC_RESOLUTION;
    _ls2k300_adc.vref_mv = ADC_VREF_MV;
    _ls2k300_adc.initialized = RT_FALSE;

    /* 初始化硬件 */
    result = ls2k300_adc_hw_init(&_ls2k300_adc);
    if (result != RT_EOK)
    {
        LOG_E("ADC hardware initialization failed: %d", result);
        return result;
    }

    rt_mutex_init(&adc_lock, "adc0", RT_IPC_FLAG_PRIO);

    /* 注册 ADC 设备 */
    result = rt_hw_adc_register(&_ls2k300_adc.parent, "adc0", &ls2k300_adc_ops, RT_NULL);
    if (result != RT_EOK)
    {
        rt_mutex_detach(&adc_lock);
        LOG_E("ADC device registration failed: %d", result);
        return result;
    }

    LOG_I("LS2K300 ADC driver initialized successfully");

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_adc_init);

#ifdef RT_USING_FINSH
#include <finsh.h>

/**
 * @brief ADC 测试命令 (finsh/msh)
 */
static int adc_test(int argc, char *argv[])
{
    rt_adc_device_t adc_dev;
    rt_uint32_t value;
    rt_int8_t channel = 0;
    rt_err_t result;

    /* 查找 ADC 设备 */
    adc_dev = (rt_adc_device_t)rt_device_find("adc0");
    if (adc_dev == RT_NULL)
    {
        rt_kprintf("Error: Cannot find adc0 device!\n");
        return -RT_ERROR;
    }

    /* 从参数获取通道 */
    if (argc > 1)
    {
        channel = (rt_int8_t)atoi(argv[1]);
        if (channel < 0 || channel > ADC_CHANNEL_MAX)
        {
            rt_kprintf("Error: Invalid channel %d, valid range is 0-%d\n", channel,
                       ADC_CHANNEL_MAX);
            return -RT_ERROR;
        }
    }

    rt_kprintf("Testing ADC channel %d...\n", channel);

    /* 使能通道 */
    result = rt_adc_enable(adc_dev, channel);
    if (result != RT_EOK)
    {
        rt_kprintf("Error: Failed to enable channel %d\n", channel);
        return result;
    }

    /* 读取 ADC 值 */
    value = rt_adc_read(adc_dev, channel);
    rt_kprintf("ADC channel %d raw value: %d\n", channel, value);

    /* 转换为电压 */
    rt_kprintf("ADC channel %d voltage: %d.%02d V\n", channel,
               (value * ADC_VREF_MV / ADC_MAX_VALUE) / 1000,
               (value * ADC_VREF_MV / ADC_MAX_VALUE) % 1000 / 10);

    /* 禁用通道 */
    result = rt_adc_disable(adc_dev, channel);
    if (result != RT_EOK)
    {
        rt_kprintf("Warning: Failed to disable channel %d\n", channel);
    }

    return RT_EOK;
}

MSH_CMD_EXPORT(adc_test, Test ADC channel(usage : adc_test[channel]));

/**
 * @brief 读取 ADC 电压命令
 */
static int adc_vol(int argc, char *argv[])
{
    rt_adc_device_t adc_dev;
    rt_uint32_t value;
    rt_int8_t channel = 0;
    rt_uint32_t vol_mv;

    /* 查找 ADC 设备 */
    adc_dev = (rt_adc_device_t)rt_device_find("adc0");
    if (adc_dev == RT_NULL)
    {
        rt_kprintf("Error: Cannot find adc0 device!\n");
        return -RT_ERROR;
    }

    /* 从参数获取通道 */
    if (argc > 1)
    {
        channel = (rt_int8_t)atoi(argv[1]);
    }

    /* 使能通道 */
    rt_adc_enable(adc_dev, channel);

    /* 读取并转换为电压 */
    value = rt_adc_read(adc_dev, channel);
    vol_mv = value * ADC_VREF_MV / ADC_MAX_VALUE;

    rt_kprintf("Channel %d: %d mV (raw: %d)\n", channel, vol_mv, value);

    /* 禁用通道 */
    rt_adc_disable(adc_dev, channel);

    return 0;
}

MSH_CMD_EXPORT(adc_vol, Read ADC voltage in mV(usage : adc_vol[channel]));

#endif /* RT_USING_FINSH */
