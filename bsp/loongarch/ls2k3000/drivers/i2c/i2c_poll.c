/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: i2c_poll.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Implement 7-bit I2C, repeated START, final NACK and bounded error recovery.
 */

#include "i2c_poll.h"

#define I2C_CONTROL     2
#define I2C_DATA        3
#define I2C_COMMAND     4
#define I2C_NACK        0x80
#define I2C_BUSY        0x40
#define I2C_ARB_LOST    0x20
#define I2C_TIP         0x02

int ls_i2c_setup(const struct ls_periph_io *io, uint32_t input, uint32_t speed)
{
    uint64_t divider;

    if (!input || !speed || speed > 400000)
    {
        return LS_INVALID;
    }

    divider = ((uint64_t)input * 2 + (uint64_t)speed * 11 - 1) / ((uint64_t)speed * 11);

    if (!divider || divider > 65536)
    {
        return LS_INVALID;
    }

    if (ls_read(io, I2C_COMMAND, 1) & (I2C_BUSY | I2C_TIP))
    {
        return LS_BUSY;
    }

    --divider;
    ls_write(io, I2C_CONTROL, 1, 0);
    ls_write(io, 0, 1, divider & 0xff);
    ls_write(io, 1, 1, divider >> 8);
    ls_write(io, I2C_COMMAND, 1, 1);
    ls_write(io, I2C_CONTROL, 1, 0x80);
    return LS_OK;
}

static int i2c_command(const struct ls_periph_io *io, unsigned int command, int check_ack)
{
    uint64_t start = io->now_us(io->context);

    ls_write(io, I2C_COMMAND, 1, command | 1);

    for (;;)
    {
        uint32_t status = ls_read(io, I2C_COMMAND, 1);

        if (status & I2C_ARB_LOST)
        {
            return LS_BUSY;
        }

        if (!(status & I2C_TIP))
        {
            return check_ack && (status & I2C_NACK) ? LS_NACK : LS_OK;
        }

        if (ls_expired(io, start, 10000))
        {
            return LS_TIMEOUT;
        }
    }
}

int ls_i2c_message(const struct ls_periph_io *io, const struct ls_i2c_message *message)
{
    int result;

    if (!message || message->address > 0x7f || message->read > 1 ||
        (message->length && !message->data))
    {
        return LS_INVALID;
    }

    ls_write(io, I2C_DATA, 1, (message->address << 1) | message->read);
    result = i2c_command(io, 0x90, 1);

    for (unsigned int i = 0; !result && i < message->length; ++i)
    {
        if (message->read)
        {
            result = i2c_command(io, 0x20 | (i + 1 == message->length ? 8 : 0), 0);

            if (!result)
            {
                message->data[i] = ls_read(io, I2C_DATA, 1);
            }
        }
        else
        {
            ls_write(io, I2C_DATA, 1, message->data[i]);
            result = i2c_command(io, 0x10, 1);
        }
    }

    return result;
}

int ls_i2c_stop(const struct ls_periph_io *io)
{
    uint64_t start = io->now_us(io->context);

    ls_write(io, I2C_COMMAND, 1, 0x41);

    while (ls_read(io, I2C_COMMAND, 1) & (I2C_BUSY | I2C_TIP))
    {
        if (ls_expired(io, start, 10000))
        {
            return LS_TIMEOUT;
        }
    }

    return LS_OK;
}

void ls_i2c_recover(const struct ls_periph_io *io)
{
    /* Never drive STOP after arbitration loss: the other master owns the bus. */
    if (ls_read(io, I2C_COMMAND, 1) & I2C_ARB_LOST)
    {
        ls_write(io, I2C_COMMAND, 1, 1);
        return;
    }

    if (ls_i2c_stop(io))
    {
        ls_write(io, I2C_CONTROL, 1, 0);
        ls_write(io, I2C_COMMAND, 1, 1);
        ls_write(io, I2C_CONTROL, 1, 0x80);
    }
}
