/*
 * Copyright (c) 2024, Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-01-01     Your Name    Initial version for LS2K0300
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: drv watchdog.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "loongson_soc_info.h"
#include "loongson_board_info.h"

#define DBG_TAG "drv.lswdt"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Register definitions from Linux driver */
#define RST_CTR  0x00    /* Reset Control Register */
#define WD_CR    0x04    /* Watchdog Control Register */
#define WD_TIMER 0x08    /* Watchdog Timer Register */

// /* Default timeout in seconds */
// #define DEFAULT_HEARTBEAT    30

#define LOONGSON_WATCHDOG_NAME "lswdt"

#ifdef APB_FREQ
#define WDT_FREQ ls2k_dev_apb_clock()
#else
#define WDT_FREQ (100000000)
#endif

/* Driver private data structure */
struct ls2x_wdt_priv
{
    rt_watchdog_t wdt_device;
    rt_uint64_t base_addr;
    rt_uint32_t clk_rate;
    rt_uint32_t timeout;
    rt_uint32_t max_hw_heartbeat;
    rt_bool_t is_start;
};

static struct ls2x_wdt_priv ls2x_wdt;

/**
 * @brief Feed the watchdog (ping operation)
 * @param wdt: watchdog device pointer
 * @return RT_EOK on success
 */
static rt_err_t ls2x_wdt_feed(rt_watchdog_t *wdt)
{
    /* Write 0x1 to WD_CR to clear watchdog counter */
    struct ls2x_wdt_priv *priv;
    priv = (struct ls2x_wdt_priv *)wdt;

    writel(0x1, (priv->base_addr + WD_CR));
    return RT_EOK;
}

/**
 * @brief Start the watchdog timer
 * @param wdt: watchdog device pointer
 * @return RT_EOK on success
 */
static rt_err_t ls2x_wdt_start(rt_watchdog_t *wdt)
{
    rt_uint8_t wd_en = 1;
    struct ls2x_wdt_priv *priv;
    priv = (struct ls2x_wdt_priv *)wdt;

    /* Enable watchdog: write (1 << 1) to RST_CTR */
    writel((1 << wd_en), (priv->base_addr + RST_CTR));
    /* reset watchdog */
    writel(0x1, (priv->base_addr + WD_CR));
    priv->is_start = RT_TRUE;

    return RT_EOK;
}

/**
 * @brief Stop the watchdog timer
 * @param wdt: watchdog device pointer
 * @return RT_EOK on success
 */
static rt_err_t ls2x_wdt_stop(rt_watchdog_t *wdt)
{
    struct ls2x_wdt_priv *priv;
    priv = (struct ls2x_wdt_priv *)wdt;

    /* Disable watchdog: write 0 to RST_CTR */
    writel(0, (priv->base_addr + RST_CTR));
    priv->is_start = RT_FALSE;

    return RT_EOK;
}

/**
 * @brief Set watchdog timeout
 * @param wdt: watchdog device pointer
 * @param timeout: timeout value in seconds
 * @return RT_EOK on success, -RT_EINVAL on error
 */
static rt_err_t ls2x_wdt_set_timeout(rt_watchdog_t *wdt, rt_uint32_t timeout)
{
    rt_uint32_t counts;
    struct ls2x_wdt_priv *priv;
    priv = (struct ls2x_wdt_priv *)wdt;

    if (timeout == 0)
    {
        return -RT_EINVAL;
    }

    /* Clamp timeout to hardware maximum */
    if (timeout > priv->max_hw_heartbeat)
    {
        timeout = priv->max_hw_heartbeat;
    }

    /* Calculate counter value: counts = clk_rate * timeout */
    counts = priv->clk_rate * timeout;

    /* Write to WD_TIMER register */
    writel(counts, (priv->base_addr + WD_TIMER));

    priv->timeout = timeout;

    return RT_EOK;
}

/**
 * @brief Get current watchdog timeout
 * @param wdt: watchdog device pointer
 * @param timeout: pointer to store timeout value
 * @return RT_EOK on success
 */
static rt_err_t ls2x_wdt_get_timeout(rt_watchdog_t *wdt, rt_uint32_t *timeout)
{
    struct ls2x_wdt_priv *priv;
    priv = (struct ls2x_wdt_priv *)wdt;

    *timeout = priv->timeout;
    return RT_EOK;
}

/**
 * @brief Watchdog device control interface
 * @param wdt: watchdog device pointer
 * @param cmd: control command
 * @param arg: argument for command
 * @return RT_EOK on success, -RT_ERROR on error
 */
static rt_err_t ls2x_wdt_control(rt_watchdog_t *wdt, int cmd, void *arg)
{
    switch (cmd)
    {
    case RT_DEVICE_CTRL_WDT_KEEPALIVE:
        /* Feed the watchdog */
        return ls2x_wdt_feed(wdt);

    case RT_DEVICE_CTRL_WDT_SET_TIMEOUT:
        /* Set timeout in seconds */
        if (arg != RT_NULL)
        {
            return ls2x_wdt_set_timeout(wdt, *(rt_uint32_t *)arg);
        }
        return -RT_EINVAL;

    case RT_DEVICE_CTRL_WDT_GET_TIMEOUT:
        /* Get current timeout */
        if (arg != RT_NULL)
        {
            return ls2x_wdt_get_timeout(wdt, (rt_uint32_t *)arg);
        }
        return -RT_EINVAL;

    case RT_DEVICE_CTRL_WDT_START:
        /* Start watchdog */
        return ls2x_wdt_start(wdt);

    case RT_DEVICE_CTRL_WDT_STOP:
        /* Stop watchdog */
        return ls2x_wdt_stop(wdt);

    default:
        return -RT_EINVAL;
    }
}

/**
 * @brief Initialize the watchdog device
 * @param wdt: watchdog device pointer
 * @return RT_EOK on success
 */
static rt_err_t ls2x_wdt_init(rt_watchdog_t *wdt)
{
    /* Set default timeout */
    // ls2x_wdt_set_timeout(wdt, DEFAULT_HEARTBEAT);

    /* Ensure watchdog is stopped initially */
    ls2x_wdt_stop(wdt);

    return RT_EOK;
}

/* Watchdog operations structure */
static struct rt_watchdog_ops ls2x_wdt_ops = {
    .init = ls2x_wdt_init,
    .control = ls2x_wdt_control,
};

/**
 * @brief Initialize and register LS2K0300 watchdog driver
 * @return RT_EOK on success, error code on failure
 */
int rt_hw_ls2x_wdt_init(void)
{
    rt_err_t ret;

    /* Initialize private data */
    ls2x_wdt.base_addr = LS2K0300_WATCHDOG_BASE;
    ls2x_wdt.is_start = RT_FALSE;

    ls2x_wdt.clk_rate = WDT_FREQ;
    ls2x_wdt.max_hw_heartbeat = (0xFFFFFFFFU / WDT_FREQ);
    ls2x_wdt.timeout = ls2x_wdt.max_hw_heartbeat;

    /* Initialize watchdog device structure */
    ls2x_wdt.wdt_device.ops = &ls2x_wdt_ops;

    /* Register watchdog device to RT-Thread framework */
    ret = rt_hw_watchdog_register(&ls2x_wdt.wdt_device, LOONGSON_WATCHDOG_NAME,
                                  RT_DEVICE_FLAG_DEACTIVATE, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_I("Failed to register watchdog device\n");
        return ret;
    }

    LOG_I("ls Watchdog driver registered [CLK: %d max timeout: %d]", ls2x_wdt.clk_rate,
          ls2x_wdt.max_hw_heartbeat);

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_ls2x_wdt_init);
