/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     OpenLANPU Contributors    test SD request failures and DMA ownership
 *
 */

/*
 * Description:
 *   Exercises the actual SDIO request engine against a deterministic MMIO model.
 *
 * Notes:
 *   Tests software ordering/error handling, not electrical timing or silicon.
 *   Build with ls2k_sdio_test.py; no SD card or root permissions are required.
 *
 */

#include "ls2k_sdio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct model
{
    struct ls2k_sdio sd;
    struct ls2k_sdio_dma dma;
    uint32_t regs[0x810 / 4];
    uint32_t now, start, events[4];
    unsigned int event_count, next_event, commands, starts, stops;
    int active, dma_active, reset_stuck, query_stuck, stop_stuck;
    int dma_incomplete, fifo_stuck;
    unsigned char media[LSD_BUFFER_SIZE];
};

static uint32_t model_read(void *context, unsigned int reg)
{
    struct model *m = context;

    assert(reg < sizeof(m->regs));
    if (reg == LSD_STATUS && m->active && m->next_event < m->event_count &&
        (uint32_t)(m->now - m->start) >= m->next_event + 1)
    {
        m->regs[reg / 4] |= m->events[m->next_event++];
    }

    return m->regs[reg / 4];
}

static void model_write(void *context, unsigned int reg, uint32_t value)
{
    struct model *m = context;

    assert(reg < sizeof(m->regs));
    if (reg == LSD_STATUS)
    {
        m->regs[reg / 4] &= ~value;
        return;
    }

    m->regs[reg / 4] = value;
    if (reg == LSD_CON && (value & LSD_RESET))
    {
        if (!m->reset_stuck)
        {
            m->regs[reg / 4] = 0;
            m->active = 0;
        }
    }

    if (reg == LSD_DMA_READ || reg == LSD_DMA_WRITE)
    {
        assert(m->regs[(reg + 4) / 4] == 0);
        assert((value & 3U) == (LSD_DMA_64BIT | LSD_DMA_UNCACHED));
        if (value & LSD_DMA_START)
        {
            assert(!m->dma_active);
            assert((value & ~31U) == m->sd.dma_address);
            assert(m->dma.transfer.device == 0x16140040);
            assert(m->dma.transfer.memory == m->sd.dma_address + 128);
            assert(m->dma.transfer.words <= LSD_BUFFER_SIZE / 4);
            assert(m->dma.transfer.repeats == 1);
            assert(!m->dma.transfer.next && !m->dma.transfer.next_high);
            assert(!m->dma.transfer.memory_high);
            assert(!!(m->dma.transfer.command & (1U << 12)) == (reg == LSD_DMA_WRITE));
            m->dma_active = 1;
            m->starts++;
            if (reg == LSD_DMA_WRITE && !m->fifo_stuck)
            {
                m->regs[LSD_FIFO_STATUS / 4] |= LSD_FIFO_TX_FULL;
            }
        }

        if (value & LSD_DMA_STOP)
        {
            m->stops++;
            if (!m->stop_stuck)
            {
                m->dma_active = 0;
            }
        }

        if ((value & LSD_DMA_QUERY) && !m->query_stuck)
        {
            assert((value & ~31U) == m->sd.dma_address + 64);
            m->dma.query.command = m->dma_active ? 0x80 : 0;
            if (m->active && !m->dma_incomplete && (uint32_t)(m->now - m->start) >= 4)
            {
                m->dma.query.command = LSD_DMA_DONE;
                m->dma_active = 0;
            }

            m->regs[reg / 4] &= ~LSD_DMA_QUERY;
        }
    }

    if (reg == LSD_CMD && (value & LSD_CMD_START))
    {
        size_t bytes = m->dma.transfer.words * 4U;

        m->active = 1;
        m->start = m->now;
        m->next_event = 0;
        m->commands++;
        if (m->dma_active)
        {
            if (m->dma.transfer.command & (1U << 12))
            {
                memcpy(m->media, m->dma.buffer, bytes);
            }
            else
            {
                memcpy(m->dma.buffer, m->media, bytes);
            }
        }

        if ((value & 63U) == 7 && !(value & LSD_CMD_RESPONSE))
        {
            m->regs[reg / 4] &= ~LSD_CMD_START;
        }
    }
}

static uint32_t model_time(void *context)
{
    return ((struct model *)context)->now;
}

static void model_wait(void *context, unsigned int ms)
{
    struct model *m = context;

    /* Even with no interrupt delivery, timed waits must poll status. */
    m->now += ms;
}

static void model_barrier(void *context)
{
    (void)context;
}

static const struct ls2k_sdio_io io =
{
    model_read, model_write, model_time, model_wait, model_barrier
};

static void initialize(struct model *m)
{
    memset(m, 0, sizeof(*m));
    m->sd.io = &io;
    m->sd.context = m;
    m->sd.dma = &m->dma;
    m->sd.dma_address = 0x00800000;
    m->sd.base_address = 0x16140000;
    m->sd.input_clock = 100000000;
    m->sd.width = 4;
    assert(ls2k_sdio_clock(&m->sd, 400000) == 0);
    assert(m->sd.clock <= 400000);
    m->events[0] = LSD_CMD_DONE;
    m->events[1] = LSD_DATA_DONE;
    m->events[2] = LSD_BUSY_DONE;
    m->event_count = 3;
    m->regs[LSD_RESPONSE / 4] = 0x12345678;
}

static struct ls2k_sdio_command data_command(void *buffer)
{
    struct ls2k_sdio_command cmd = {0};

    cmd.index = 18;
    cmd.response_present = 1;
    cmd.response_crc = 1;
    cmd.buffer = buffer;
    cmd.block_size = 512;
    cmd.blocks = LSD_BUFFER_SIZE / 512;
    cmd.timeout_ms = 20;
    return cmd;
}

int main(void)
{
    static struct model m;
    static unsigned char guarded[LSD_BUFFER_SIZE + 2];
    struct ls2k_sdio_command cmd;
    unsigned int before;

    initialize(&m);
    assert(ls2k_sdio_clock(&m.sd, 1) == LSD_INVALID);
    assert(ls2k_sdio_clock(&m.sd, 25000000) == 0);
    assert(m.sd.clock == 25000000);
    assert(ls2k_sdio_clock(&m.sd, 0) == 0 && !m.sd.clock);
    assert(ls2k_sdio_clock(&m.sd, 400000) == 0);

    cmd = data_command(guarded + 1);
    memset(m.media, 0xa5, sizeof(m.media));
    guarded[0] = 0x12;
    guarded[sizeof(guarded) - 1] = 0x34;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
    assert(!memcmp(guarded + 1, m.media, LSD_BUFFER_SIZE));
    assert(guarded[0] == 0x12 && guarded[sizeof(guarded) - 1] == 0x34);
    assert(cmd.response[0] == 0x12345678 && m.now >= 4);

    initialize(&m);
    m.events[0] = LSD_DATA_DONE;
    m.events[1] = LSD_CMD_DONE;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
    initialize(&m);
    m.events[0] = LSD_DATA_DONE | LSD_CMD_DONE;
    m.event_count = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);

    /* 200 MiB written and read in 16 KiB transactions, including bounce copies. */
    initialize(&m);
    for (unsigned int block = 0; block < 12800; block++)
    {
        for (unsigned int i = 0; i < LSD_BUFFER_SIZE; i++)
        {
            guarded[i + 1] = (i * 17U + block * 23U) & 255U;
        }

        cmd = data_command(guarded + 1);
        cmd.index = 25;
        cmd.write = 1;
        assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
        memset(guarded + 1, 0, LSD_BUFFER_SIZE);
        cmd.index = 18;
        cmd.write = 0;
        assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
        for (unsigned int i = 0; i < LSD_BUFFER_SIZE; i++)
        {
            assert(guarded[i + 1] == ((i * 17U + block * 23U) & 255U));
        }
    }

    /* Failed reads must never copy partially DMA-written contents to callers. */
    const uint32_t failures[] = {LSD_DATA_CRC, LSD_RESPONSE_CRC, LSD_CMD_TIMEOUT,
                                LSD_DATA_TIMEOUT, LSD_PROGRAM_ERROR};
    for (unsigned int i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
    {
        initialize(&m);
        cmd = data_command(guarded + 1);
        memset(guarded + 1, 0x5a, LSD_BUFFER_SIZE);
        m.events[0] = failures[i] | LSD_CMD_DONE | LSD_DATA_DONE;
        assert(ls2k_sdio_transfer(&m.sd, &cmd) < 0);
        assert(m.stops == 1 && !m.sd.fault);
        for (unsigned int j = 1; j <= LSD_BUFFER_SIZE; j++)
        {
            assert(guarded[j] == 0x5a);
        }

        m.events[0] = LSD_CMD_DONE;
        assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
    }

    initialize(&m);
    m.event_count = 0;
    m.now = UINT32_MAX - 8;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_TIMEOUT);
    assert(m.stops == 1 && !m.sd.fault);

    initialize(&m);
    m.query_stuck = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_FAULT);
    before = m.commands;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_FAULT);
    assert(m.commands == before);

    initialize(&m);
    m.event_count = 0;
    m.stop_stuck = m.dma_incomplete = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_FAULT);
    assert(m.sd.fault && m.dma_active);

    initialize(&m);
    m.dma_incomplete = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_TIMEOUT);
    assert(!m.sd.fault && m.stops == 1);

    initialize(&m);
    m.reset_stuck = 1;
    assert(ls2k_sdio_reset(&m.sd) == LSD_FAULT);

    initialize(&m);
    cmd.index = 25;
    cmd.write = 1;
    m.fifo_stuck = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_TIMEOUT);
    assert(!m.commands && m.stops == 1);

    initialize(&m);
    cmd = data_command(guarded + 1);
    cmd.blocks = UINT32_MAX;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_INVALID);
    cmd.blocks = 1;
    cmd.block_size = 3;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_INVALID);
    cmd.blocks = 4096;
    cmd.block_size = 4;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_INVALID);
    cmd.blocks = 1;
    m.sd.dma_address++;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_INVALID);
    m.sd.dma_address = 0x10000000 - 64;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_INVALID);
    assert(!m.commands && !m.starts);

    initialize(&m);
    cmd = data_command(guarded + 1);
    cmd.index = 6;
    cmd.block_size = 64;
    cmd.blocks = 1;
    memset(m.media, 0x96, 64);
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);
    assert(guarded[1] == 0x69 && guarded[64] == 0x69);

    initialize(&m);
    cmd = (struct ls2k_sdio_command){.index = 12, .response_present = 1,
                                  .response_busy = 1, .timeout_ms = 10};
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0 && m.now == 3);
    initialize(&m);
    m.event_count = 1;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == LSD_TIMEOUT);

    initialize(&m);
    cmd = (struct ls2k_sdio_command){.index = 7, .timeout_ms = 10};
    m.event_count = 0;
    assert(ls2k_sdio_transfer(&m.sd, &cmd) == 0);

    puts("SDIO: 200 MiB round trip, ordering, bounds, CRC, timeout, recovery and DMA quarantine PASS");
    return 0;
}
