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
 *   Bounded native SD/eMMC requests for LS2K3000 (manual chapters 33/34).
 *   Adapted from the project LS2K0300 engine; uses 10-bit division and eMMC widths.
 *   DMA remains in dedicated bounce storage and is quarantined on stop failure.
 */

#ifndef LS2K3000_MMC_POLL_H
#define LS2K3000_MMC_POLL_H

#include <stddef.h>
#include <stdint.h>

#define LSD_CON         0x00
#define LSD_PRE         0x04
#define LSD_ARG         0x08
#define LSD_CMD         0x0c
#define LSD_RESPONSE    0x14
#define LSD_TIMER       0x24
#define LSD_BLOCK_SIZE  0x28
#define LSD_DATA        0x2c
#define LSD_FIFO_STATUS 0x38
#define LSD_STATUS      0x3c
#define LSD_FIFO        0x40
#define LSD_IRQ_ENABLE  0x64
#define LSD_BUS_MODE    0xfc
#define LSD_DMA_WRITE   0x400
#define LSD_DMA_READ    0x800

#define LSD_RESET         (1U << 8)
#define LSD_CLOCK         1U
#define LSD_CMD_START     (1U << 8)
#define LSD_CMD_RESPONSE  (1U << 9)
#define LSD_CMD_LONG      (1U << 10)
#define LSD_CMD_CHECK_CRC (1U << 13)
#define LSD_CMD6_DATA     (1U << 18)
#define LSD_DATA_START    (1U << 14)
#define LSD_DATA_DMA      (1U << 15)
#define LSD_DATA_WIDE8    (1U << 26)
#define LSD_DATA_WIDE     (1U << 16)
#define LSD_FIFO_TX_FULL  (1U << 11)

#define LSD_DATA_DONE     (1U << 0)
#define LSD_DATA_TIMEOUT  (1U << 1)
#define LSD_DATA_CRC      ((1U << 2) | (1U << 3))
#define LSD_PROGRAM_ERROR (1U << 4)
#define LSD_CMD_DONE      (1U << 6)
#define LSD_CMD_TIMEOUT   (1U << 7)
#define LSD_RESPONSE_CRC  (1U << 8)
#define LSD_BUSY_DONE     (1U << 9)
#define LSD_ALL_STATUS    0x3ffU
#define LSD_REQUEST_IRQS  (LSD_ALL_STATUS & ~(1U << 5))

#define LSD_DMA_64BIT    1U
#define LSD_DMA_UNCACHED 2U
#define LSD_DMA_QUERY    4U
#define LSD_DMA_START    8U
#define LSD_DMA_STOP     16U
#define LSD_DMA_DONE     8U
#define LSD_DMA_STATES   0xff0U
#define LSD_BUFFER_SIZE  (16U * 1024U)

#define LSD_OK      0
#define LSD_TIMEOUT (-1)
#define LSD_CRC     (-2)
#define LSD_INVALID (-3)
#define LSD_IO      (-4)
#define LSD_FAULT   (-5)

struct ls2k3000_mmc_desc
{
    uint32_t next;
    uint32_t memory;
    uint32_t device;
    uint32_t words;
    uint32_t stride;
    uint32_t repeats;
    uint32_t command;
    uint32_t reserved;
    uint32_t next_high;
    uint32_t memory_high;
    uint32_t padding[6];
};

struct ls2k3000_mmc_dma
{
    struct ls2k3000_mmc_desc transfer;
    volatile struct ls2k3000_mmc_desc query;
    uint8_t buffer[LSD_BUFFER_SIZE];
};

struct ls2k3000_mmc_io
{
    uint32_t (*read)(void *context, unsigned int reg);
    void (*write)(void *context, unsigned int reg, uint32_t value);
    uint32_t (*time_ms)(void *context);
    void (*wait_ms)(void *context, unsigned int ms);
    void (*barrier)(void *context);
};

struct ls2k3000_mmc_command
{
    unsigned int index;
    uint32_t argument;
    uint32_t response[4];
    int response_present;
    int response_long;
    int response_crc;
    int response_busy;
    void *buffer;
    unsigned int block_size;
    unsigned int blocks;
    int write;
    unsigned int timeout_ms;
};

struct ls2k3000_mmc
{
    const struct ls2k3000_mmc_io *io;
    void *context;
    struct ls2k3000_mmc_dma *dma;
    uint32_t dma_address;
    uint32_t base_address;
    uint32_t input_clock;
    uint32_t clock;
    unsigned int width;
    int emmc;
    int fault;
};

int ls2k3000_mmc_quiesce(struct ls2k3000_mmc *sd);
int ls2k3000_mmc_reset(struct ls2k3000_mmc *sd);
int ls2k3000_mmc_clock(struct ls2k3000_mmc *sd, uint32_t requested);
int ls2k3000_mmc_transfer(struct ls2k3000_mmc *sd, struct ls2k3000_mmc_command *cmd);

#endif
