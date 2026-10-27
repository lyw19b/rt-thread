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
 *   LS2K0300 development BSP: drv spi io.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_SPI_IO_H__
#define __DRV_SPI_IO_H__

#include <rtthread.h>
#include <rtdevice.h>

#include "loongson_board_info.h"

/* SPI controller numbers for LS2K300 */
#define LS2K300_SPI2 2
#define LS2K300_SPI3 3

/* SPI configuration options */
#define SPI_IO_REF_CLK ls2k_dev_apb_clock()  /* MHz */

/* DMA configuration for SPI */
#define SPI_DMA_CHANNEL_BASE(spi_num) ((spi_num) == 2 ? 4 : 2) //根据手册决定
#define SPI_DMA_TX_CHANNEL(spi_num)   (SPI_DMA_CHANNEL_BASE(spi_num) + 1)
#define SPI_DMA_RX_CHANNEL(spi_num)   (SPI_DMA_CHANNEL_BASE(spi_num))

int rt_hw_spi_io_init(void);
rt_err_t rt_hw_spi_io_device_attach(const char *bus_name, const char *device_name, rt_base_t cs_pin,
                                    void *user_data);

#endif /* __DRV_SPI_IO_H__ */
