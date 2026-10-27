/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: periph_io.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Provide testable register access and bounded peripheral operations.
 */

#ifndef LS2K3000_PERIPH_IO_H
#define LS2K3000_PERIPH_IO_H

#include <stdint.h>
#include <stddef.h>

enum ls_periph_result
{
    LS_OK = 0,
    LS_INVALID = -1,
    LS_TIMEOUT = -2,
    LS_BUSY = -3,
    LS_NACK = -4,
    LS_IO_ERROR = -5,
    LS_UNSUPPORTED = -6,
    LS_DISABLED = -7
};

struct ls_periph_io
{
    void *context;
    uint32_t (*read)(void *context, unsigned int offset, unsigned int width);
    void (*write)(void *context, unsigned int offset, unsigned int width, uint32_t value);
    uint64_t (*now_us)(void *context);
};

static inline uint32_t ls_read(const struct ls_periph_io *io, unsigned int reg,
                               unsigned int width)
{
    return io->read(io->context, reg, width);
}

static inline void ls_write(const struct ls_periph_io *io, unsigned int reg,
                            unsigned int width, uint32_t value)
{
    io->write(io->context, reg, width, value);
}

static inline int ls_expired(const struct ls_periph_io *io, uint64_t start,
                             uint64_t timeout)
{
    return io->now_us(io->context) - start >= timeout;
}

#endif
