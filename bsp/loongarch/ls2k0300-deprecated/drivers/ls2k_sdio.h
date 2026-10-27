/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     OpenLANPU Contributors    add LS2K0300 SD memory controller
 *
 */

/*
 * Description:
 *   Defines the SDIO 1.2 register interface and bounded request engine.
 *
 * Notes:
 *   Register facts follow the LS2K0300 User Manual, chapters 15 and 16,
 *   and open-loongarch/linux-6.12 drivers/mmc/host/ls2kmci.c.
 *   The caller serializes requests and supplies uncached, low-DDR DMA storage.
 *
 */

#ifndef LS2K_SDIO_H__
#define LS2K_SDIO_H__

#include <stddef.h>
#include <stdint.h>

#define LSD_CON             0x00
#define LSD_PRE             0x04
#define LSD_ARG             0x08
#define LSD_CMD             0x0c
#define LSD_RESPONSE        0x14
#define LSD_TIMER           0x24
#define LSD_BLOCK_SIZE      0x28
#define LSD_DATA            0x2c
#define LSD_FIFO_STATUS     0x38
#define LSD_STATUS          0x3c
#define LSD_FIFO            0x40
#define LSD_IRQ_ENABLE      0x64
#define LSD_BUS_MODE        0xfc
#define LSD_DMA_WRITE       0x400
#define LSD_DMA_READ        0x800

#define LSD_RESET           (1U << 8)
#define LSD_CLOCK           1U
#define LSD_CMD_START       (1U << 8)
#define LSD_CMD_RESPONSE    (1U << 9)
#define LSD_CMD_LONG        (1U << 10)
#define LSD_CMD_CHECK_CRC   (1U << 13)
#define LSD_CMD6_DATA       (1U << 18)
#define LSD_DATA_START      (1U << 14)
#define LSD_DATA_DMA        (1U << 15)
#define LSD_DATA_WIDE       (1U << 16)
#define LSD_FIFO_TX_FULL    (1U << 11)

#define LSD_DATA_DONE       (1U << 0)
#define LSD_DATA_TIMEOUT    (1U << 1)
#define LSD_DATA_CRC        ((1U << 2) | (1U << 3))
#define LSD_PROGRAM_ERROR   (1U << 4)
#define LSD_CMD_DONE        (1U << 6)
#define LSD_CMD_TIMEOUT     (1U << 7)
#define LSD_RESPONSE_CRC    (1U << 8)
#define LSD_BUSY_DONE       (1U << 9)
#define LSD_ALL_STATUS      0x3ffU
#define LSD_REQUEST_IRQS    (LSD_ALL_STATUS & ~(1U << 5))

#define LSD_DMA_64BIT       1U
#define LSD_DMA_UNCACHED    2U
#define LSD_DMA_QUERY       4U
#define LSD_DMA_START       8U
#define LSD_DMA_STOP        16U
#define LSD_DMA_DONE        8U
#define LSD_DMA_STATES      0xff0U
#define LSD_BUFFER_SIZE     (16U * 1024U)

#define LSD_OK              0
#define LSD_TIMEOUT         (-1)
#define LSD_CRC             (-2)
#define LSD_INVALID         (-3)
#define LSD_IO              (-4)
#define LSD_FAULT           (-5)

struct ls2k_sdio_desc
{
    uint32_t next, memory, device, words, stride, repeats, command, reserved;
    uint32_t next_high, memory_high;
    uint32_t padding[6];
};

struct ls2k_sdio_dma
{
    struct ls2k_sdio_desc transfer;
    volatile struct ls2k_sdio_desc query;
    uint8_t buffer[LSD_BUFFER_SIZE];
};

struct ls2k_sdio_io
{
    uint32_t (*read)(void *context, unsigned int reg);
    void (*write)(void *context, unsigned int reg, uint32_t value);
    uint32_t (*time_ms)(void *context);
    void (*wait_ms)(void *context, unsigned int ms);
    void (*barrier)(void *context);
};

struct ls2k_sdio_command
{
    unsigned int index;
    uint32_t argument, response[4];
    int response_present, response_long, response_crc, response_busy;
    void *buffer;
    unsigned int block_size, blocks;
    int write;
    unsigned int timeout_ms;
};

struct ls2k_sdio
{
    const struct ls2k_sdio_io *io;
    void *context;
    struct ls2k_sdio_dma *dma;
    uint32_t dma_address, base_address, input_clock, clock;
    unsigned int width;
    int fault;
};

int ls2k_sdio_reset(struct ls2k_sdio *sd);
int ls2k_sdio_clock(struct ls2k_sdio *sd, uint32_t requested);
int ls2k_sdio_transfer(struct ls2k_sdio *sd, struct ls2k_sdio_command *cmd);

#endif
