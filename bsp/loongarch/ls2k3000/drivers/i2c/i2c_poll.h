/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: i2c_poll.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Describe native I2C messages independently of the OS adapter.
 */

#ifndef LS2K3000_I2C_POLL_H
#define LS2K3000_I2C_POLL_H

#include "periph_io.h"

struct ls_i2c_message
{
    uint8_t address;
    uint8_t read;
    uint16_t length;
    uint8_t *data;
};

int ls_i2c_setup(const struct ls_periph_io *io, uint32_t input, uint32_t speed);
int ls_i2c_message(const struct ls_periph_io *io, const struct ls_i2c_message *message);
int ls_i2c_stop(const struct ls_periph_io *io);
void ls_i2c_recover(const struct ls_periph_io *io);

#endif
