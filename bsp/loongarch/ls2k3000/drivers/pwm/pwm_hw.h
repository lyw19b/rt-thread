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
 *   Describe checked native PWM register operations.
 * Notes:
 *   The caller serializes channel configuration; period and pulse are in nanoseconds.
 */

#ifndef LS2K3000_PWM_HW_H
#define LS2K3000_PWM_HW_H

#include "periph_io.h"

int ls_pwm_set(const struct ls_periph_io *io, unsigned int channel,
               uint32_t period_ns, uint32_t pulse_ns);
int ls_pwm_enable(const struct ls_periph_io *io, unsigned int channel, int enable);
int ls_pwm_get(const struct ls_periph_io *io, unsigned int channel,
               uint32_t *period_ns, uint32_t *pulse_ns);

#endif
