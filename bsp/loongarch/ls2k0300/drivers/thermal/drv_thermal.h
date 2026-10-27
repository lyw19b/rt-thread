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
 *   LS2K0300 development BSP: drv thermal.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_THERMAL_H__
#define __DRV_THERMAL_H__

#include <rtthread.h>
#include <rtdevice.h>

int rt_hw_ls2k300_thermal_init(void);

#endif /* __DRV_THERMAL_H__ */
