/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef _ASM_TIMER_H
#define _ASM_TIMER_H


#define CPU_HZ       (200 * 1000 * 1000)

void rt_hw_timer_init(void);

void rt_hw_timer_handler(void);

#endif /* _ASM_TIMER_H */
