/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: pwm_hw.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Describe checked native PWM register operations.
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
