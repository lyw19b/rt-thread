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
 *   Describe native SPI state and firmware register preservation.
 * Notes:
 *   The caller holds the bus lock across the entire chip-select chain.
 */

#ifndef LS2K3000_SPI_POLL_H
#define LS2K3000_SPI_POLL_H

#include "periph_io.h"

struct ls_spi_state
{
    struct ls_periph_io io;
    uint8_t saved[4];
    uint8_t active;
    uint8_t cs;
};

int ls_spi_divider(uint32_t input, uint32_t speed);
int ls_spi_begin(struct ls_spi_state *state, uint32_t input, uint32_t speed,
                 unsigned int mode, unsigned int cs);
int ls_spi_bytes(struct ls_spi_state *state, const uint8_t *tx, uint8_t *rx, size_t length);
void ls_spi_end(struct ls_spi_state *state);

#endif
