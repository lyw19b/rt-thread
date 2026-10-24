/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     OpenLANPU Contributors    implement bounded SDIO DMA requests
 *
 */

/*
 * Description:
 *   Executes SD memory commands through the LS2K0300 SDIO 1.2 controller.
 *
 * Notes:
 *   This is an independent implementation of the documented register protocol.
 *   DMA only accesses dedicated bounce storage, never caller/user buffers.
 *   A failed DMA stop quarantines the storage until reboot.
 *
 */

#include "ls2k_sdio.h"
#include <string.h>

_Static_assert(sizeof(struct ls2k_sdio_desc) == 64, "DMA descriptors must stay aligned");
_Static_assert(offsetof(struct ls2k_sdio_dma, buffer) == 128, "DMA buffer alignment");

static uint32_t read_reg(struct ls2k_sdio *sd, unsigned int reg)
{
    return sd->io->read(sd->context, reg);
}

static void write_reg(struct ls2k_sdio *sd, unsigned int reg, uint32_t value)
{
    sd->io->write(sd->context, reg, value);
}

static int wait_clear(struct ls2k_sdio *sd, unsigned int reg,
                      uint32_t mask, unsigned int timeout)
{
    uint32_t start = sd->io->time_ms(sd->context);

    while (read_reg(sd, reg) & mask)
    {
        if ((uint32_t)(sd->io->time_ms(sd->context) - start) >= timeout)
        {
            return LSD_TIMEOUT;
        }

        sd->io->wait_ms(sd->context, 1);
    }

    return LSD_OK;
}

int ls2k_sdio_clock(struct ls2k_sdio *sd, uint32_t requested)
{
    uint32_t divider;

    if (!requested)
    {
        write_reg(sd, LSD_CON, 0);
        sd->clock = 0;
        return LSD_OK;
    }

    if (!sd->input_clock)
    {
        return LSD_INVALID;
    }

    divider = sd->input_clock / requested + !!(sd->input_clock % requested);
    if (!divider || divider > 255)
    {
        return LSD_INVALID;
    }

    write_reg(sd, LSD_CON, 0);
    write_reg(sd, LSD_PRE, (1U << 31) | divider);
    write_reg(sd, LSD_CON, LSD_CLOCK);
    sd->clock = sd->input_clock / divider;
    return LSD_OK;
}

int ls2k_sdio_reset(struct ls2k_sdio *sd)
{
    uint32_t pre = read_reg(sd, LSD_PRE);
    uint32_t mode = read_reg(sd, LSD_BUS_MODE);

    write_reg(sd, LSD_IRQ_ENABLE, 0);
    write_reg(sd, LSD_CON, LSD_RESET);
    if (wait_clear(sd, LSD_CON, LSD_RESET, 100))
    {
        sd->fault = 1;
        return LSD_FAULT;
    }

    write_reg(sd, LSD_BUS_MODE, mode);
    write_reg(sd, LSD_PRE, pre);
    write_reg(sd, LSD_CON, sd->clock ? LSD_CLOCK : 0);
    write_reg(sd, LSD_STATUS, LSD_ALL_STATUS);
    return LSD_OK;
}

static void dma_order(struct ls2k_sdio *sd, unsigned int reg, uint32_t address,
                      uint32_t operation)
{
    sd->io->barrier(sd->context);
    /* SDIO 1.2 latches the order on the low-word write, unlike the old IP. */
    write_reg(sd, reg + 4, 0);
    write_reg(sd, reg, address | LSD_DMA_64BIT | LSD_DMA_UNCACHED | operation);
}

static int dma_state(struct ls2k_sdio *sd, unsigned int reg, uint32_t *state)
{
    sd->dma->query.command = UINT32_MAX;
    dma_order(sd, reg, sd->dma_address + offsetof(struct ls2k_sdio_dma, query), LSD_DMA_QUERY);
    if (wait_clear(sd, reg, LSD_DMA_QUERY, 20))
    {
        return LSD_TIMEOUT;
    }

    sd->io->barrier(sd->context);
    *state = sd->dma->query.command;
    return LSD_OK;
}

static int dma_finish(struct ls2k_sdio *sd, unsigned int reg, int stop)
{
    uint32_t start = sd->io->time_ms(sd->context);
    uint32_t state;

    if (stop)
    {
        dma_order(sd, reg, sd->dma_address, LSD_DMA_STOP);
        sd->io->wait_ms(sd->context, 1);
    }

    do
    {
        if (dma_state(sd, reg, &state))
        {
            return LSD_TIMEOUT;
        }

        if ((state & LSD_DMA_DONE) || (stop && !(state & LSD_DMA_STATES)))
        {
            return LSD_OK;
        }

        sd->io->wait_ms(sd->context, 1);
    } while ((uint32_t)(sd->io->time_ms(sd->context) - start) < 100);

    return LSD_TIMEOUT;
}

static int command_status(uint32_t status, const struct ls2k_sdio_command *cmd)
{
    if (status & (LSD_CMD_TIMEOUT | LSD_DATA_TIMEOUT))
    {
        return LSD_TIMEOUT;
    }

    /* The vendor driver documents spurious CRC failure on 136-bit replies.
     * R3/R4 have no CRC; normal short responses must still be checked. */
    if ((status & LSD_DATA_CRC) ||
        ((status & LSD_RESPONSE_CRC) && cmd->response_crc && !cmd->response_long))
    {
        return LSD_CRC;
    }

    return status & LSD_PROGRAM_ERROR ? LSD_IO : LSD_OK;
}

static uint8_t reverse_byte(uint8_t byte)
{
    byte = ((byte & 0x55U) << 1) | ((byte >> 1) & 0x55U);
    byte = ((byte & 0x33U) << 2) | ((byte >> 2) & 0x33U);
    return (byte << 4) | (byte >> 4);
}

int ls2k_sdio_transfer(struct ls2k_sdio *sd, struct ls2k_sdio_command *cmd)
{
    unsigned int dma_reg = cmd->write ? LSD_DMA_WRITE : LSD_DMA_READ;
    size_t bytes = 0;
    uint32_t command, required, seen = 0, start;
    int result = LSD_OK;
    int dma_started = 0;

    if (sd->fault)
    {
        return LSD_FAULT;
    }

    if (cmd->index > 63 || !cmd->timeout_ms || cmd->timeout_ms > 10000 ||
        !sd->clock || (sd->width != 1 && sd->width != 4))
    {
        return LSD_INVALID;
    }

    if (cmd->buffer)
    {
        if (!sd->dma || (sd->dma_address & 63U) ||
            sd->dma_address >= 0x10000000U - sizeof(*sd->dma) ||
            !cmd->block_size || cmd->block_size > 512 || !cmd->blocks || cmd->blocks > 4095 ||
            cmd->blocks > LSD_BUFFER_SIZE / cmd->block_size || (cmd->block_size & 3))
        {
            return LSD_INVALID;
        }

        bytes = (size_t)cmd->blocks * cmd->block_size;
    }
    else if (cmd->block_size || cmd->blocks)
    {
        return LSD_INVALID;
    }

    write_reg(sd, LSD_IRQ_ENABLE, 0);
    write_reg(sd, LSD_STATUS, LSD_ALL_STATUS);
    write_reg(sd, LSD_DATA, 0);
    required = LSD_CMD_DONE;
    command = cmd->index | 0x40U | LSD_CMD_START;

    if (cmd->response_present)
    {
        command |= LSD_CMD_RESPONSE;
    }

    if (cmd->response_long)
    {
        command |= LSD_CMD_LONG;
    }

    if (cmd->response_crc && !cmd->response_long)
    {
        command |= LSD_CMD_CHECK_CRC;
    }

    if (cmd->response_busy)
    {
        required |= LSD_BUSY_DONE;
    }

    if (bytes)
    {
        struct ls2k_sdio_desc *desc = &sd->dma->transfer;

        if (cmd->write)
        {
            memcpy(sd->dma->buffer, cmd->buffer, bytes);
        }

        memset(desc, 0, sizeof(*desc));
        desc->memory = sd->dma_address + offsetof(struct ls2k_sdio_dma, buffer);
        desc->device = sd->base_address + LSD_FIFO;
        desc->words = bytes / 4;
        desc->repeats = 1;
        desc->command = 1U | (cmd->write ? 1U << 12 : 0);
        write_reg(sd, LSD_BLOCK_SIZE, cmd->block_size);
        write_reg(sd, LSD_TIMER, 0x00ffffffU);

        if (!cmd->write)
        {
            dma_order(sd, dma_reg, sd->dma_address, LSD_DMA_START);
        }

        write_reg(sd, LSD_DATA, cmd->blocks | LSD_DATA_START | LSD_DATA_DMA |
                  (sd->width == 4 ? LSD_DATA_WIDE : 0));

        if (cmd->write)
        {
            dma_order(sd, dma_reg, sd->dma_address, LSD_DMA_START);
        }

        dma_started = 1;
        required |= LSD_DATA_DONE;
        if (cmd->index == 6)
        {
            command |= LSD_CMD6_DATA;
        }

        /* Start multi-block writes only after the FIFO has been prefetched.
         * The vendor driver applies this workaround to the shared IP. */
        if (cmd->index == 25)
        {
            start = sd->io->time_ms(sd->context);
            while (!(read_reg(sd, LSD_FIFO_STATUS) & LSD_FIFO_TX_FULL))
            {
                if ((uint32_t)(sd->io->time_ms(sd->context) - start) >= 20)
                {
                    result = LSD_TIMEOUT;
                    goto out;
                }

                sd->io->wait_ms(sd->context, 1);
            }
        }
    }

    write_reg(sd, LSD_ARG, cmd->argument);
    write_reg(sd, LSD_IRQ_ENABLE, LSD_REQUEST_IRQS);
    write_reg(sd, LSD_CMD, command);
    start = sd->io->time_ms(sd->context);

    for (;;)
    {
        uint32_t status = read_reg(sd, LSD_STATUS);

        write_reg(sd, LSD_STATUS, status);
        seen |= status;
        result = command_status(seen, cmd);
        if (result || (seen & required) == required)
        {
            break;
        }

        /* Deselect has no response and may not raise CMD_DONE on this IP. */
        if (cmd->index == 7 && !cmd->argument && !cmd->response_present &&
            !(read_reg(sd, LSD_CMD) & LSD_CMD_START))
        {
            break;
        }

        if ((uint32_t)(sd->io->time_ms(sd->context) - start) >= cmd->timeout_ms)
        {
            result = LSD_TIMEOUT;
            break;
        }

        sd->io->wait_ms(sd->context, 1);
    }

    if (!result)
    {
        for (unsigned int index = 0; index < 4; index++)
        {
            cmd->response[index] = read_reg(sd, LSD_RESPONSE + index * 4);
        }

        if (dma_started)
        {
            result = dma_finish(sd, dma_reg, 0);
        }
    }

out:
    write_reg(sd, LSD_IRQ_ENABLE, 0);
    if (result)
    {
        /* Never reset/reuse a buffer while an unconfirmed DMA can write it. */
        if (dma_started && dma_finish(sd, dma_reg, 1))
        {
            sd->fault = 1;
            return LSD_FAULT;
        }

        if (ls2k_sdio_reset(sd))
        {
            return LSD_FAULT;
        }
    }
    else if (bytes && !cmd->write)
    {
        sd->io->barrier(sd->context);
        if (cmd->index == 6)
        {
            /* SDIO 1.2 CMD6 payload has reversed bits in each byte. */
            for (size_t index = 0; index < bytes; index++)
            {
                sd->dma->buffer[index] = reverse_byte(sd->dma->buffer[index]);
            }
        }

        memcpy(cmd->buffer, sd->dma->buffer, bytes);
    }

    write_reg(sd, LSD_CMD, 0);
    write_reg(sd, LSD_DATA, 0);
    write_reg(sd, LSD_STATUS, LSD_ALL_STATUS);
    return result;
}
