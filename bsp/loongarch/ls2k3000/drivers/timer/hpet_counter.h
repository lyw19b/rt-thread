/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: hpet_counter.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Expose a read-only HPET counter interface, without advertising IRQ alarms.
 */

#ifndef LS2K3000_HPET_COUNTER_H
#define LS2K3000_HPET_COUNTER_H

#include "periph_io.h"

int ls_hpet_read(const struct ls_periph_io *io, uint64_t *ticks);
int ls_hpet_period(const struct ls_periph_io *io, uint32_t *femtoseconds);

/* rt_device_read(hpet0, 0, &ticks, sizeof(ticks)); control returns fs per tick. */
#define LS2K3000_HPET_GET_PERIOD 0x3001

#endif
