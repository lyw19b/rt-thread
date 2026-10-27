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
 *   Program the 50 MHz watchdog without echoing the adjacent software reset bit.
 * Notes:
 *   Never echo RST_CNT bit 0: writing it requests an immediate board reset.
 */

#include "watchdog_hw.h"

int ls_wdt_timeout(const struct ls_periph_io *io, unsigned int seconds)
{
    if (!seconds || seconds > UINT32_MAX / 50000000U)
    {
        return LS_INVALID;
    }

    ls_write(io, 8, 4, seconds * 50000000U);

    if (ls_read(io, 0, 4) & 2)
    {
        ls_write(io, 4, 4, 1);
    }

    return LS_OK;
}

int ls_wdt_enable(const struct ls_periph_io *io, int enable)
{
    if (enable && !ls_read(io, 8, 4))
    {
        return LS_INVALID;
    }

    /* RST_CNT[0] reboots immediately; never read-modify-write it. */
    ls_write(io, 0, 4, enable ? 2 : 0);

    if (enable)
    {
        ls_write(io, 4, 4, 1);
    }

    return LS_OK;
}

int ls_wdt_feed(const struct ls_periph_io *io)
{
    if (!(ls_read(io, 0, 4) & 2))
    {
        return LS_DISABLED;
    }

    ls_write(io, 4, 4, 1);
    return LS_OK;
}
