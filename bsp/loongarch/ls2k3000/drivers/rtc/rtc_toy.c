/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Validate TOY dates, handle rollover and bound asynchronous write completion.
 * Notes:
 *   Use UTC and the firmware year-minus-1900 convention; do not reset the oscillator.
 */

#include "rtc_toy.h"

#define TOY_ENABLE      ((1U << 11) | (1U << 8))

static unsigned int month_days(unsigned int year, unsigned int month)
{
    static const unsigned int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    return days[month - 1] + (month == 2 && !(year % 4) && (year % 100 || !(year % 400)));
}

static int rtc_ready(const struct ls_periph_io *io)
{
    uint64_t start = io->now_us(io->context);

    for (;;)
    {
        uint32_t control = ls_read(io, 0x40, 4);

        if ((control & TOY_ENABLE) != TOY_ENABLE)
        {
            return LS_DISABLED;
        }

        if (!(control & 1))
        {
            return LS_OK;
        }

        if (ls_expired(io, start, 100000))
        {
            return LS_TIMEOUT;
        }
    }
}

int ls_rtc_read(const struct ls_periph_io *io, int64_t *seconds, unsigned int *tenths)
{
    uint32_t low = 0;
    uint32_t year = 0;
    unsigned int attempt;
    int result;

    if (!seconds || !tenths)
    {
        return LS_INVALID;
    }

    result = rtc_ready(io);

    if (result)
    {
        return result;
    }

    for (attempt = 0; attempt < 8; ++attempt)
    {
        low = ls_read(io, 0x2c, 4);
        year = ls_read(io, 0x30, 4);

        if (low == ls_read(io, 0x2c, 4))
        {
            break;
        }
    }

    if (attempt == 8)
    {
        return LS_BUSY;
    }

    unsigned int month = low >> 26;
    unsigned int day = (low >> 21) & 31;
    unsigned int hour = (low >> 16) & 31;
    unsigned int minute = (low >> 10) & 63;
    unsigned int second = (low >> 4) & 63;
    int64_t days = 0;

    /* Firmware/Linux convention stores tm_year, not the full calendar year. */
    if (year < 70 || year > 8099 || month < 1 || month > 12 || day < 1 ||
        hour > 23 || minute > 59 || second > 59 || (low & 15) > 9)
    {
        return LS_INVALID;
    }

    year += 1900;

    if (day > month_days(year, month))
    {
        return LS_INVALID;
    }

    for (unsigned int y = 1970; y < year; ++y)
    {
        days += 337 + month_days(y, 2);
    }

    for (unsigned int m = 1; m < month; ++m)
    {
        days += month_days(year, m);
    }

    *seconds = ((days + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
    *tenths = low & 15;
    return LS_OK;
}

int ls_rtc_write(const struct ls_periph_io *io, int64_t seconds, unsigned int tenths)
{
    unsigned int year = 1970;
    unsigned int month = 1;
    unsigned int remaining;
    uint32_t low;
    int64_t days;
    int result;

    if (seconds < 0 || seconds > INT64_C(253402300799) || tenths > 9)
    {
        return LS_INVALID;
    }

    days = seconds / 86400;
    remaining = seconds % 86400;

    while (days >= (int64_t)(337 + month_days(year, 2)))
    {
        days -= 337 + month_days(year++, 2);
    }

    while (days >= (int64_t)month_days(year, month))
    {
        days -= month_days(year, month++);
    }

    low = (month << 26) | ((uint32_t)(days + 1) << 21) |
          ((remaining / 3600) << 16) | (((remaining / 60) % 60) << 10) |
          ((remaining % 60) << 4) | tenths;
    result = rtc_ready(io);

    if (result)
    {
        return result;
    }

    /* Do not reset the battery-backed clock or trim during device registration. */
    ls_write(io, 0x28, 4, year - 1900);
    result = rtc_ready(io);

    if (!result)
    {
        ls_write(io, 0x24, 4, low);
        result = rtc_ready(io);
    }

    return result;
}
