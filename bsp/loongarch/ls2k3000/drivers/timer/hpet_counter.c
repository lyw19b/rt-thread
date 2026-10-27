/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: hpet_counter.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Read a live 64-bit HPET counter using high-low-high sampling.
 */

#include "hpet_counter.h"

int ls_hpet_period(const struct ls_periph_io *io, uint32_t *femtoseconds)
{
    uint32_t caps = ls_read(io, 0, 4);
    uint32_t period = ls_read(io, 4, 4);

    if (!femtoseconds || !(caps & 0xff) || !(caps & (1U << 13)) ||
        !period || period > 100000000)
    {
        return LS_UNSUPPORTED;
    }

    *femtoseconds = period;
    return LS_OK;
}

int ls_hpet_read(const struct ls_periph_io *io, uint64_t *ticks)
{
    if (!ticks)
    {
        return LS_INVALID;
    }

    if (!(ls_read(io, 0x10, 4) & 1))
    {
        return LS_DISABLED;
    }

    for (unsigned int attempt = 0; attempt < 8; ++attempt)
    {
        uint32_t high = ls_read(io, 0xf4, 4);
        uint32_t low = ls_read(io, 0xf0, 4);

        if (high == ls_read(io, 0xf4, 4))
        {
            *ticks = ((uint64_t)high << 32) | low;
            return LS_OK;
        }
    }

    return LS_BUSY;
}
