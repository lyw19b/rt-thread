/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: watchdog_hw.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Expose bounded watchdog configuration without automatic arming.
 */

#ifndef LS2K3000_WATCHDOG_HW_H
#define LS2K3000_WATCHDOG_HW_H

#include "periph_io.h"

int ls_wdt_timeout(const struct ls_periph_io *io, unsigned int seconds);
int ls_wdt_enable(const struct ls_periph_io *io, int enable);
int ls_wdt_feed(const struct ls_periph_io *io);

#endif
