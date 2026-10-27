/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: drv interrupt.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_INTERRUPT_H__
#define __DRV_INTERRUPT_H__

#include <rtthread.h>
#include <rthw.h>
#include <drivers/dev_pin.h>
#include "rtcompiler.h"
#include "rttypes.h"
#include "irq.h"

void rt_hw_interrupt_init();
rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler, void *param,
                                         const char *name);

#endif
