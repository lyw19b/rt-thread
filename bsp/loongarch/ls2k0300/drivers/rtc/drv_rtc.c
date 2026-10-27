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
 *   LS2K0300 development BSP: drv rtc.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <sys/time.h>
#include <drv_rtc.h>

#include "loongson_soc_info.h"

#define DBG_TAG "drv.rtc"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* RTC Control Register bit definitions */
#define RTC_CTRL_TOY_ENABLE (1 << 11)   /* TOY timer enable */
#define RTC_CTRL_RTC_ENABLE (1 << 8)    /* RTC enable */

/**
 * @brief Convert hardware RTC data to struct tm
 * @param hw_data Pointer to hardware RTC data structure
 * @param tm Pointer to struct tm to fill
 */
void rtc_to_tm(const struct rtc_data *hw_data, struct tm *tm)
{
    rt_memset(tm, 0, sizeof(struct tm));

    tm->tm_year = hw_data->year - 1900;     /* struct tm year is years since 1900 */
    tm->tm_mon = hw_data->month - 1;       /* struct tm month is 0-11 */
    tm->tm_mday = hw_data->day;           /* Day of month: 1-31 */
    tm->tm_hour = hw_data->hour;          /* Hour: 0-23 */
    tm->tm_min = hw_data->minute;        /* Minute: 0-59 */
    tm->tm_sec = hw_data->second;        /* Second: 0-59 */

    /* Calculate day of week (0-6, Sunday = 0) using Zeller's congruence algorithm */
    int year = hw_data->year;
    int month = hw_data->month;
    int day = hw_data->day;

    if (month < 3)
    {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int wday = (day + 13 * (month + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    tm->tm_wday = (wday + 6) % 7;  /* Convert to Sunday = 0 format */

    /* Calculate day of year (0-365) */
    static const int days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int doy = days_before_month[tm->tm_mon] + day - 1;
    /* Add leap day if applicable */
    if (tm->tm_mon > 1 &&
        ((hw_data->year % 4 == 0 && hw_data->year % 100 != 0) || (hw_data->year % 400 == 0)))
    {
        doy++;
    }
    tm->tm_yday = doy;

    tm->tm_isdst = 0;  /* Daylight saving time flag */
}

/**
 * @brief Convert struct tm to hardware RTC data
 * @param tm Pointer to struct tm
 * @param hw_data Pointer to hardware RTC data structure to fill
 */
static void tm_to_rtc(const struct tm *tm, struct rtc_data *hw_data)
{
    hw_data->year = (rt_uint16_t)(tm->tm_year + 1900);
    hw_data->month = (rt_uint8_t)(tm->tm_mon + 1);
    hw_data->day = (rt_uint8_t)tm->tm_mday;
    hw_data->hour = (rt_uint8_t)tm->tm_hour;
    hw_data->minute = (rt_uint8_t)tm->tm_min;
    hw_data->second = (rt_uint8_t)tm->tm_sec;
    hw_data->ms_100 = 0;  /* Set to 0 by default */
}

/**
 * @brief Read current time from LS2K300 RTC hardware
 * @param hw_data Pointer to hardware RTC data structure to fill
 * @return RT_EOK on success, -RT_ERROR on failure
 */
static rt_err_t rtc_validate(const struct rtc_data *hw_data);

rt_err_t rtc_read_time(struct rtc_data *hw_data)
{
    rt_uint32_t val0, val1;

    if (hw_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Read TOY registers (Read0 contains time, Read1 contains year) */
    val0 = readl(LS_TOY_READ0_REG);
    val1 = readl(LS_TOY_READ1_REG);

    /* Parse register values according to hardware bit field definitions:
     * TOY_READ0_REG layout:
     * [31:26] - Month (6 bits)
     * [25:21] - Day (5 bits)
     * [20:16] - Hour (5 bits)
     * [15:10] - Minute (6 bits)
     * [9:4]   - Second (6 bits)
     * [3:0]   - 100ms units (4 bits)
     */
    hw_data->year = (rt_uint16_t)(val1 + 1900);
    hw_data->month = (rt_uint8_t)((val0 >> 26) & 0x3f);
    hw_data->day = (rt_uint8_t)((val0 >> 21) & 0x1f);
    hw_data->hour = (rt_uint8_t)((val0 >> 16) & 0x1f);
    hw_data->minute = (rt_uint8_t)((val0 >> 10) & 0x3f);
    hw_data->second = (rt_uint8_t)((val0 >> 4) & 0x3f);
    hw_data->ms_100 = (rt_uint8_t)(val0 & 0x0f);

    return rtc_validate(hw_data);
}

/**
 * @brief Write time to LS2K300 RTC hardware
 * @param hw_data Pointer to hardware RTC data structure containing time to set
 * @return RT_EOK on success, -RT_EINVAL on invalid parameter
 */
rt_err_t rtc_write_time(const struct rtc_data *hw_data)
{
    rt_uint32_t val0, val1;

    if (hw_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (rtc_validate(hw_data) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    /* Pack time values into register format */
    val0 = ((hw_data->month & 0x3f) << 26) | ((hw_data->day & 0x1f) << 21) |
           ((hw_data->hour & 0x1f) << 16) | ((hw_data->minute & 0x3f) << 10) |
           ((hw_data->second & 0x3f) << 4) | ((hw_data->ms_100 & 0x0f));

    val1 = hw_data->year - 1900;

    /* Write to TOY registers */
    /* Set the year first so February 29 is interpreted in the target year. */
    writel(val1, LS_TOY_WRITE1_REG);
    writel(val0, LS_TOY_WRITE0_REG);

    return RT_EOK;
}

/**
 * @brief Validate RTC time data
 * @param hw_data Pointer to hardware RTC data structure to validate
 * @return RT_EOK if valid, -RT_ERROR if invalid
 */
static rt_err_t rtc_validate(const struct rtc_data *hw_data)
{
    static const rt_uint8_t days_in_month[13] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (hw_data == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Check year range (reasonable range: 2000-2099) */
    if (hw_data->year < 2000 || hw_data->year > 2099)
    {
        LOG_W("RTC year out of range: %d", hw_data->year);
        return -RT_ERROR;
    }

    /* Check month range (1-12) */
    if (hw_data->month < 1 || hw_data->month > 12)
    {
        LOG_W("RTC month out of range: %d", hw_data->month);
        return -RT_ERROR;
    }

    /* Calculate days in month, accounting for leap year */
    rt_uint8_t max_day = days_in_month[hw_data->month];
    if (hw_data->month == 2)
    {
        /* Leap year check: divisible by 4, not by 100 unless by 400 */
        if ((hw_data->year % 4 == 0 && hw_data->year % 100 != 0) || (hw_data->year % 400 == 0))
        {
            max_day = 29;
        }
    }

    /* Check day range */
    if (hw_data->day < 1 || hw_data->day > max_day)
    {
        LOG_W("RTC day out of range: %d (max %d)", hw_data->day, max_day);
        return -RT_ERROR;
    }

    /* Check hour range (0-23) */
    if (hw_data->hour > 23)
    {
        LOG_W("RTC hour out of range: %d", hw_data->hour);
        return -RT_ERROR;
    }

    /* Check minute range (0-59) */
    if (hw_data->minute > 59)
    {
        LOG_W("RTC minute out of range: %d", hw_data->minute);
        return -RT_ERROR;
    }

    /* Check second range (0-59) */
    if (hw_data->second > 59)
    {
        LOG_W("RTC second out of range: %d", hw_data->second);
        return -RT_ERROR;
    }

    /* Check 100ms units range (0-9) */
    if (hw_data->ms_100 > 9)
    {
        LOG_W("RTC 100ms units out of range: %d", hw_data->ms_100);
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* RTC device structure */
static struct rt_rtc_device rtc_dev;

/**
 * @brief Initialize LS2K300 RTC hardware
 * @return RT_EOK on success
 */
static rt_err_t rtc_init(void)
{
    rt_uint32_t ctrl_val;

    /* Clear trim registers */
    writel(0, LS_TOY_TRIM_REG);
    writel(0, LS_RTC_TRIM_REG);

    /* Configure and enable RTC */
    ctrl_val = RTC_CTRL_TOY_ENABLE | RTC_CTRL_RTC_ENABLE;
    writel(ctrl_val, LS_RTC_CTRL_REG);

    return RT_EOK;
}

/**
 * @brief Get current time in seconds since epoch
 * @param sec Pointer to store time_t value
 * @return RT_EOK on success, -RT_EINVAL on invalid parameter
 */
static rt_err_t rtc_get_secs(time_t *sec)
{
    struct rtc_data hw_data;
    struct tm tm;
    rt_err_t ret;

    if (sec == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Read from hardware */
    ret = rtc_read_time(&hw_data);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* Validate read data */
    ret = rtc_validate(&hw_data);
    if (ret != RT_EOK)
    {
        /* Return error if data is invalid (e.g., after power loss) */
        LOG_W("RTC data invalid, may need initialization");
        return ret;
    }

    /* Convert to struct tm */
    rtc_to_tm(&hw_data, &tm);

    /* Convert to time_t (seconds since epoch) */
    *sec = timegm(&tm);

    LOG_D("RTC get_secs: %ld", (long)*sec);

    return RT_EOK;
}

/**
 * @brief Set time using seconds since epoch
 * @param sec Pointer to time_t value to set
 * @return RT_EOK on success, -RT_EINVAL on invalid parameter
 */
static rt_err_t rtc_set_secs(time_t *sec)
{
    struct rtc_data hw_data;
    struct tm storage;
    struct tm *tm;

    if (sec == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Convert time_t to struct tm */
    tm = gmtime_r(sec, &storage);
    if (tm == RT_NULL)
    {
        return -RT_ERROR;
    }

    /* Convert struct tm to hardware format */
    tm_to_rtc(tm, &hw_data);

    /* Validate before writing */
    if (rtc_validate(&hw_data) != RT_EOK)
    {
        LOG_E("Invalid time value for RTC");
        return -RT_ERROR;
    }

    /* Write to hardware */
    rtc_write_time(&hw_data);

    LOG_I("RTC time set to: %04d-%02d-%02d %02d:%02d:%02d", hw_data.year, hw_data.month,
          hw_data.day, hw_data.hour, hw_data.minute, hw_data.second);

    return RT_EOK;
}

/**
 * @brief Get high-resolution time (with microseconds)
 * @param tv Pointer to struct timeval to fill
 * @return RT_EOK on success, -RT_EINVAL on invalid parameter
 */
static rt_err_t rtc_get_timeval(struct timeval *tv)
{
    struct rtc_data hw_data;
    struct tm tm;
    rt_err_t ret;

    if (tv == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Read from hardware */
    ret = rtc_read_time(&hw_data);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* Validate read data */
    ret = rtc_validate(&hw_data);
    if (ret != RT_EOK)
    {
        return ret;
    }

    /* Convert to struct tm */
    rtc_to_tm(&hw_data, &tm);

    /* Convert to time_t */
    tv->tv_sec = timegm(&tm);

    /* Convert 100ms units to microseconds (0-9 * 100000) */
    tv->tv_usec = hw_data.ms_100 * 100000;

    LOG_D("RTC get_timeval: %ld.%06ld", (long)tv->tv_sec, (long)tv->tv_usec);

    return RT_EOK;
}

/**
 * @brief Set high-resolution time
 * @param tv Pointer to struct timeval containing time to set
 * @return RT_EOK on success, -RT_EINVAL on invalid parameter
 */
static rt_err_t rtc_set_timeval(struct timeval *tv)
{
    struct rtc_data hw_data;
    struct tm storage;
    struct tm *tm;

    if (tv == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* Convert time_t to struct tm */
    tm = gmtime_r(&tv->tv_sec, &storage);
    if (tm == RT_NULL)
    {
        return -RT_ERROR;
    }

    /* Convert struct tm to hardware format */
    tm_to_rtc(tm, &hw_data);

    /* Convert microseconds to 100ms units (0-9) */
    hw_data.ms_100 = (rt_uint8_t)((tv->tv_usec / 100000) % 10);

    /* Validate before writing */
    if (rtc_validate(&hw_data) != RT_EOK)
    {
        LOG_E("Invalid time value for RTC");
        return -RT_ERROR;
    }

    /* Write to hardware */
    rtc_write_time(&hw_data);

    return RT_EOK;
}

#ifdef RT_USING_ALARM
/**
 * @brief Get alarm configuration
 * @param alarm Pointer to rt_rtc_wkalarm structure to fill
 * @return RT_EOK (alarm not supported by hardware, return default values)
 */
static rt_err_t rtc_get_alarm(struct rt_rtc_wkalarm *alarm)
{
    if (alarm == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /* LS2K300 hardware alarm support is limited, return disabled state */
    rt_memset(alarm, 0, sizeof(struct rt_rtc_wkalarm));
    alarm->enable = 0;

    LOG_W("Hardware alarm not fully supported on LS2K300");

    return RT_EOK;
}

/**
 * @brief Set alarm configuration
 * @param alarm Pointer to rt_rtc_wkalarm structure containing alarm settings
 * @return RT_EOK if set successfully, -RT_ERROR if not supported
 */
static rt_err_t rtc_set_alarm(struct rt_rtc_wkalarm *alarm)
{
    if (alarm == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /*
     * LS2K300 has TOY_MATCH registers but they are typically used for
     * periodic interrupts rather than single-shot alarms.
     * For full alarm support, software emulation is recommended.
     */
    LOG_W("Hardware alarm setting not implemented on LS2K300");
    LOG_W("Use RT-Thread software alarm component instead");

    /*
     * If needed, the TOY_MATCH registers could be configured here:
     * LS_TOY_MATCH0_REG, LS_TOY_MATCH1_REG, LS_TOY_MATCH2_REG
     */

    return -RT_ERROR;
}
#endif /* RT_USING_ALARM */

/* RTC operations structure */
static const struct rt_rtc_ops rtc_ops = {
    .init = rtc_init,
    .get_secs = rtc_get_secs,
    .set_secs = rtc_set_secs,
    .get_timeval = rtc_get_timeval,
    .set_timeval = rtc_set_timeval,
#ifdef RT_USING_ALARM
    .get_alarm = rtc_get_alarm,
    .set_alarm = rtc_set_alarm,
#endif
};

/**
 * @brief Register LS2K300 RTC device to RT-Thread system
 * @return RT_EOK on success, -RT_ERROR on failure
 */
int rt_hw_rtc_init(void)
{
    rt_err_t ret;

    /* Initialize hardware first */
    ret = rtc_init();
    if (ret != RT_EOK)
    {
        LOG_E("Failed to initialize LS2K300 RTC hardware");
        return ret;
    }

    /* Setup RTC device structure */
    rtc_dev.ops = &rtc_ops;

    /* Register RTC device with RT-Thread */
    ret = rt_hw_rtc_register(&rtc_dev, RTC_DEVICE_NAME, RT_DEVICE_FLAG_RDWR, RT_NULL);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to register RTC device, error: %d", ret);
        return ret;
    }

    LOG_I("rtc device registered successfully");

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_rtc_init);
