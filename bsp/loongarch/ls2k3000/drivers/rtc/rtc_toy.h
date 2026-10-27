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
 *   Expose UTC TOY calendar conversion and stable time snapshots.
 * Notes:
 *   The caller serializes reads and writes; firmware must enable the TOY oscillator.
 */

#ifndef LS2K3000_RTC_TOY_H
#define LS2K3000_RTC_TOY_H

#include "periph_io.h"

int ls_rtc_read(const struct ls_periph_io *io, int64_t *seconds, unsigned int *tenths);
int ls_rtc_write(const struct ls_periph_io *io, int64_t seconds, unsigned int tenths);

#endif
