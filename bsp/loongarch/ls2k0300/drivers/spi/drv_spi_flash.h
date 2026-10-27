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
 *   LS2K0300 development BSP: drv spi flash.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_SPI_FLASH_H__
#define __DRV_SPI_FLASH_H__

#include <rtthread.h>
#include <rtdevice.h>

int rt_hw_spi_flash_init(void);
rt_err_t rt_hw_spi_flash_device_attach(const char *bus_name, const char *device_name,
                                       rt_base_t cs_pin, void *user_data);
#endif /* __DRV_SPI_FLASH_H__ */
