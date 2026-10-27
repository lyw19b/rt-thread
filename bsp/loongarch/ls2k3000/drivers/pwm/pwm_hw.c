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
 *   Program 48 MHz PWM counters with output masked during updates.
 * Notes:
 *   Do not take over capture or interrupt channels; reject unsupported duty cycles.
 */

#include "pwm_hw.h"

int ls_pwm_set(const struct ls_periph_io *io, unsigned int channel,
               uint32_t period_ns, uint32_t pulse_ns)
{
    uint64_t period = (uint64_t)period_ns * 48 / 1000;
    uint64_t high = (uint64_t)pulse_ns * 48 / 1000;
    unsigned int offset = channel * 0x100;
    uint32_t old;

    if (channel >= 4 || !pulse_ns || pulse_ns >= period_ns ||
        period > UINT32_MAX || high < 1 || period <= high)
    {
        return LS_INVALID;
    }

    old = ls_read(io, offset + 12, 4);
    /* Refuse to take over a capture/interrupt/dead-zone owner. */
    if (old & ((1U << 4) | (1U << 5) | (1U << 8) | (1U << 9) | (1U << 10)))
    {
        return LS_BUSY;
    }

    ls_write(io, offset + 12, 4, 0x88);
    ls_write(io, offset + 4, 4, period - high);
    ls_write(io, offset + 8, 4, period);
    ls_write(io, offset + 12, 4, (old & 1) ? (old & 9) : 8);
    return LS_OK;
}

int ls_pwm_enable(const struct ls_periph_io *io, unsigned int channel, int enable)
{
    unsigned int offset = channel * 0x100;
    uint32_t control;

    if (channel >= 4)
    {
        return LS_INVALID;
    }

    control = ls_read(io, offset + 12, 4);

    if (control & ((1U << 4) | (1U << 5) | (1U << 8) | (1U << 9) | (1U << 10)))
    {
        return LS_BUSY;
    }

    if (enable)
    {
        uint32_t low = ls_read(io, offset + 4, 4);
        uint32_t full = ls_read(io, offset + 8, 4);

        if (!low || low >= full)
        {
            return LS_INVALID;
        }
    }

    ls_write(io, offset + 12, 4, enable ? 1 : 8);
    return LS_OK;
}

int ls_pwm_get(const struct ls_periph_io *io, unsigned int channel,
               uint32_t *period_ns, uint32_t *pulse_ns)
{
    unsigned int offset = channel * 0x100;
    uint32_t full;
    uint32_t low;

    if (channel >= 4 || !period_ns || !pulse_ns)
    {
        return LS_INVALID;
    }

    full = ls_read(io, offset + 8, 4);
    low = ls_read(io, offset + 4, 4);

    if (!low || low >= full || (uint64_t)full * 1000 / 48 > UINT32_MAX)
    {
        return LS_INVALID;
    }

    *period_ns = (uint64_t)full * 1000 / 48;
    *pulse_ns = (uint64_t)(full - low) * 1000 / 48;
    return LS_OK;
}
