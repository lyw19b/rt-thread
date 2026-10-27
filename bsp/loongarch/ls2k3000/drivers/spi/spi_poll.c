/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: spi_poll.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Transfer native SPI bytes and restore firmware flash read configuration.
 */

#include "spi_poll.h"

static const unsigned int divisors[] = {
    2, 4, 16, 32, 8, 64, 128, 256, 512, 1024, 2048, 4096
};

int ls_spi_divider(uint32_t input, uint32_t speed)
{
    int best = LS_INVALID;

    if (!input || !speed)
    {
        return LS_INVALID;
    }

    for (unsigned int i = 0; i < sizeof(divisors) / sizeof(divisors[0]); ++i)
    {
        if ((uint64_t)speed * divisors[i] >= input &&
            (best < 0 || divisors[i] < divisors[best]))
        {
            best = (int)i;
        }
    }

    return best;
}

int ls_spi_begin(struct ls_spi_state *state, uint32_t input, uint32_t speed,
                 unsigned int mode, unsigned int cs)
{
    const struct ls_periph_io *io = &state->io;
    int divider = ls_spi_divider(input, speed);

    if (divider < 0 || mode > 3 || cs > 3)
    {
        return LS_INVALID;
    }

    if (state->active)
    {
        return LS_BUSY;
    }

    state->saved[0] = ls_read(io, 0, 1);
    state->saved[1] = ls_read(io, 3, 1);
    state->saved[2] = ls_read(io, 4, 1);
    state->saved[3] = ls_read(io, 5, 1);
    state->active = 1;
    state->cs = cs;
    ls_write(io, 5, 1, 0xff);
    ls_write(io, 4, 1, state->saved[2] & ~1U);
    ls_write(io, 0, 1, 0x50 | (mode << 2) | (divider & 3));
    ls_write(io, 3, 1, (state->saved[1] & ~3U) | ((unsigned int)divider >> 2));
    ls_write(io, 1, 1, 0xc0);

    /* Discard stale receive bytes, bounded even if the controller is wedged. */
    for (unsigned int i = 0; i < 16 && !(ls_read(io, 1, 1) & 1); ++i)
    {
        (void)ls_read(io, 2, 1);
    }

    if (!(ls_read(io, 1, 1) & 1))
    {
        ls_spi_end(state);
        return LS_IO_ERROR;
    }

    ls_write(io, 5, 1, 0xff & ~(0x10U << cs));
    return LS_OK;
}

void ls_spi_end(struct ls_spi_state *state)
{
    const struct ls_periph_io *io = &state->io;

    if (!state->active)
    {
        return;
    }

    ls_write(io, 5, 1, 0xff);
    ls_write(io, 0, 1, state->saved[0]);
    ls_write(io, 3, 1, state->saved[1]);
    ls_write(io, 4, 1, state->saved[2]);
    ls_write(io, 5, 1, state->saved[3]);
    state->active = 0;
}

static int spi_wait(const struct ls_periph_io *io, unsigned int busy_mask)
{
    uint64_t start = io->now_us(io->context);

    for (;;)
    {
        uint32_t status = ls_read(io, 1, 1);

        if (status & 0x40)
        {
            return LS_IO_ERROR;
        }

        if (!(status & busy_mask))
        {
            return LS_OK;
        }

        if (ls_expired(io, start, 10000))
        {
            return LS_TIMEOUT;
        }
    }
}

int ls_spi_bytes(struct ls_spi_state *state, const uint8_t *tx, uint8_t *rx, size_t length)
{
    const struct ls_periph_io *io = &state->io;
    int result = LS_OK;

    if (!state->active)
    {
        return LS_INVALID;
    }

    for (size_t i = 0; i < length; ++i)
    {
        result = spi_wait(io, 8);

        if (result)
        {
            break;
        }

        ls_write(io, 2, 1, tx ? tx[i] : 0xff);
        result = spi_wait(io, 1);

        if (result)
        {
            break;
        }

        uint8_t value = ls_read(io, 2, 1);

        if (rx)
        {
            rx[i] = value;
        }
    }

    if (result)
    {
        ls_spi_end(state);
    }

    return result;
}
