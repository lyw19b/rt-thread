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
 *   LS2K0300 development BSP: drv power.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include "drv_power.h"

void rt_hw_cpu_reset(void)
{
    /* Reset/PMIC wiring is board specific; no guessed reset register writes. */
    rt_kprintf("Reset through the board reset button or firmware.\n");
}
