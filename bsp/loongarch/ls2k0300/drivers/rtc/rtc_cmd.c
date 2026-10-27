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
 *   LS2K0300 development BSP: rtc cmd.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <drv_rtc.h>
#include "loongson_soc_info.h"

#ifdef RT_USING_FINSH
#include <finsh.h>

/**
 * @brief Print current RTC time for debugging
 */
static void rtc_dump(void)
{
    struct rtc_data hw_data;
    rt_err_t ret;

    ret = rtc_read_time(&hw_data);
    if (ret != RT_EOK)
    {
        rt_kprintf("Failed to read RTC: %d\n", ret);
        return;
    }

    rt_kprintf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d.%d\n", hw_data.year, hw_data.month,
               hw_data.day, hw_data.hour, hw_data.minute, hw_data.second, hw_data.ms_100 * 100);

    /* Also show as timestamp */
    struct tm tm;
    rtc_to_tm(&hw_data, &tm);
    time_t ts = mktime(&tm);
    rt_kprintf("Timestamp: %ld\n", (long)ts);
}

MSH_CMD_EXPORT(rtc_dump, dump LS2K300 RTC registers);

/* Alarm event flag */
static rt_event_t rtc_alarm_event = RT_NULL;

/* Forward declarations */
static void rtc_alarm_callback(rt_int32_t irq, void *param);
static void cmd_set_time(int argc, char **argv);
static void cmd_show_time(int argc, char **argv);
static void cmd_set_alarm(int argc, char **argv);

/**
 * @brief Set system time and RTC time
 * Usage: set_time 2025 2 23 14 30 0
 */
static void cmd_set_time(int argc, char **argv)
{
    struct tm new_time = { 0 };
    struct timeval tv;
    rt_err_t ret;
    rt_device_t rtc_device = RT_NULL;

    if (argc != 7)
    {
        rt_kprintf("Usage: set_time <year> <month> <day> <hour> <minute> <second>\n");
        rt_kprintf("Example: set_time 2025 2 23 14 30 0\n");
        return;
    }
    rtc_device = rt_device_find(RTC_DEVICE_NAME);

    /* Parse arguments */
    new_time.tm_year = atoi(argv[1]) - 1900;  /* Years since 1900 */
    new_time.tm_mon = atoi(argv[2]) - 1;       /* 0-11 */
    new_time.tm_mday = atoi(argv[3]);          /* 1-31 */
    new_time.tm_hour = atoi(argv[4]);          /* 0-23 */
    new_time.tm_min = atoi(argv[5]);           /* 0-59 */
    new_time.tm_sec = atoi(argv[6]);           /* 0-59 */

    /* Validate */
    if (new_time.tm_year < 100 || new_time.tm_year > 199)
    { /* 2000-2099 */
        rt_kprintf("Error: Year must be between 2000 and 2099\n");
        return;
    }
    if (new_time.tm_mon < 0 || new_time.tm_mon > 11)
    {
        rt_kprintf("Error: Month must be between 1 and 12\n");
        return;
    }
    if (new_time.tm_mday < 1 || new_time.tm_mday > 31)
    {
        rt_kprintf("Error: Day must be between 1 and 31\n");
        return;
    }
    if (new_time.tm_hour < 0 || new_time.tm_hour > 23)
    {
        rt_kprintf("Error: Hour must be between 0 and 23\n");
        return;
    }
    if (new_time.tm_min < 0 || new_time.tm_min > 59)
    {
        rt_kprintf("Error: Minute must be between 0 and 59\n");
        return;
    }
    if (new_time.tm_sec < 0 || new_time.tm_sec > 59)
    {
        rt_kprintf("Error: Second must be between 0 and 59\n");
        return;
    }

    /* Convert to timestamp */
    tv.tv_sec = mktime(&new_time);
    tv.tv_usec = 0;

    /* Set RTC time using control command */
    ret = rt_device_control(rtc_device, RT_DEVICE_CTRL_RTC_SET_TIME, &tv.tv_sec);
    if (ret != RT_EOK)
    {
        rt_kprintf("Failed to set RTC time: %d\n", ret);
        return;
    }

    /* Also set system time */
    ret = settimeofday(&tv, RT_NULL);
    if (ret != 0)
    {
        rt_kprintf("Warning: settimeofday failed, but RTC is set\n");
    }

    rt_kprintf("Time set successfully to: %04d-%02d-%02d %02d:%02d:%02d\n", new_time.tm_year + 1900,
               new_time.tm_mon + 1, new_time.tm_mday, new_time.tm_hour, new_time.tm_min,
               new_time.tm_sec);
}

MSH_CMD_EXPORT(cmd_set_time, set RTC time : set_time YYYY MM DD HH MM SS);

/**
 * @brief Show current time from RTC
 */
static void cmd_show_time(int argc, char **argv)
{
    struct tm timeinfo;
    time_t now;
    struct timeval tv;
    rt_err_t ret;
    rt_device_t rtc_device = RT_NULL;

    (void)argc;
    (void)argv;
    rtc_device = rt_device_find(RTC_DEVICE_NAME);

    /* Method 1: Use time() function */
    now = time(RT_NULL);
    localtime_r(&now, &timeinfo);

    rt_kprintf("\n========== Current RTC Time ==========\n");
    rt_kprintf("Method 1 (time()):\n");
    rt_kprintf("  Date: %04d-%02d-%02d\n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
               timeinfo.tm_mday);
    rt_kprintf("  Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    rt_kprintf("  Weekday: %d (0=Sun, 6=Sat)\n", timeinfo.tm_wday);
    rt_kprintf("  Day of year: %d\n", timeinfo.tm_yday);
    rt_kprintf("  Unix timestamp: %ld\n", (long)now);

    /* Method 2: Direct device control for high precision */
    ret = rt_device_control(rtc_device, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &tv);
    if (ret == RT_EOK)
    {
        rt_kprintf("\nMethod 2 (High precision):\n");
        rt_kprintf("  Seconds: %ld\n", (long)tv.tv_sec);
        rt_kprintf("  Microseconds: %ld\n", (long)tv.tv_usec);
        rt_kprintf("  High-res timestamp: %ld.%06ld\n", (long)tv.tv_sec, (long)tv.tv_usec);
    }

    rt_kprintf("======================================\n\n");
}

MSH_CMD_EXPORT(cmd_show_time, display current RTC time in multiple formats);

#endif /* RT_USING_FINSH */
