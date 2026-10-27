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

#ifndef __DRV_RTC_LS2K300_H__
#define __DRV_RTC_LS2K300_H__

#include <rtthread.h>

/**
 * @brief Internal structure for LS2K300 RTC date/time data
 *
 * This structure mirrors the hardware register layout for the TOY (Time of Year)
 * counter which maintains the real-time clock value.
 */
struct rtc_data
{
    rt_uint16_t year;       /* Year (e.g., 2025) */
    rt_uint8_t month;      /* Month: 1-12 */
    rt_uint8_t day;        /* Day: 1-31 */
    rt_uint8_t hour;       /* Hour: 0-23 */
    rt_uint8_t minute;     /* Minute: 0-59 */
    rt_uint8_t second;     /* Second: 0-59 */
    rt_uint8_t ms_100;     /* 100ms units: 0-9 (0.1 second resolution) */
};

#define RTC_DEVICE_NAME "rtc"

/* Export initialization for manual call if needed */
int rt_hw_rtc_init(void);

rt_err_t rtc_read_time(struct rtc_data *hw_data);
void rtc_to_tm(const struct rtc_data *hw_data, struct tm *tm);
rt_err_t rtc_write_time(const struct rtc_data *hw_data);

#endif /* __DRV_RTC_LS2K300_H__ */
