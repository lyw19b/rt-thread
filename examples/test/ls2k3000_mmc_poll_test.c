/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               retain DMA quiescence regression
 */

/*
 * Description:
 *   Exercise the production MMC engine with bounded DMA query fault injection.
 *   A previous transfer's DONE bit must not release a currently busy engine.
 */

#include "mmc_poll.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct dma_model
{
    struct ls2k3000_mmc_dma storage;
    uint32_t registers[0x808 / 4];
    uint32_t state;
    uint32_t clock;
    unsigned int queries;
    unsigned int stops;
    unsigned int busy_queries;
    int query_stuck;
    uint32_t card_status;
    unsigned int commands;
    unsigned int ready_after;
    int command_timeout;
};

static uint32_t model_read(void *context, unsigned int reg)
{
    struct dma_model *model = context;

    assert(reg < sizeof(model->registers));
    return model->registers[reg / 4];
}

static void model_write(void *context, unsigned int reg, uint32_t value)
{
    struct dma_model *model = context;

    assert(reg < sizeof(model->registers));

    if (reg == LSD_STATUS)
    {
        model->registers[reg / 4] &= ~value;
        return;
    }

    model->registers[reg / 4] = value;

    if (reg == LSD_CON && (value & LSD_RESET))
    {
        model->registers[reg / 4] &= ~LSD_RESET;
    }

    if (reg == LSD_CMD && (value & LSD_CMD_START))
    {
        assert((value & 63U) == 13);
        ++model->commands;
        model->registers[LSD_STATUS / 4] = model->command_timeout ?
                                         LSD_CMD_TIMEOUT : LSD_CMD_DONE;
        model->registers[LSD_RESPONSE / 4] = model->ready_after &&
            model->commands > model->ready_after ? 0x900U : model->card_status;
    }

    if (reg != LSD_DMA_READ && reg != LSD_DMA_WRITE)
    {
        return;
    }

    if (value & LSD_DMA_STOP)
    {
        ++model->stops;
    }

    if (value & LSD_DMA_QUERY)
    {
        ++model->queries;

        if (!model->query_stuck)
        {
            model->registers[reg / 4] &= ~LSD_DMA_QUERY;
            model->storage.query.command = model->busy_queries &&
                model->queries > model->busy_queries ? 0x500U : model->state;
        }
    }
}

static uint32_t model_time(void *context)
{
    return ((struct dma_model *)context)->clock;
}

static void model_wait(void *context, unsigned int milliseconds)
{
    ((struct dma_model *)context)->clock += milliseconds;
}

static void model_barrier(void *context)
{
    (void)context;
}

static const struct ls2k3000_mmc_io model_io =
{
    .read = model_read,
    .write = model_write,
    .time_ms = model_time,
    .wait_ms = model_wait,
    .barrier = model_barrier,
};

static void check_state(uint32_t state, int expected)
{
    struct dma_model model = {.state = state};
    struct ls2k3000_mmc controller =
    {
        .io = &model_io,
        .context = &model,
        .dma = &model.storage,
        .dma_address = 0x07e00000,
    };

    int result = ls2k3000_mmc_quiesce(&controller);

    printf("DMA state=%08x result=%d time=%u queries=%u\n",
           state, result, model.clock, model.queries);
    fflush(stdout);
    assert(result == expected);
    assert(controller.fault == (expected != LSD_OK));
    assert(model.clock <= 202);

    if (expected == LSD_OK)
    {
        assert(model.stops == 2);
    }
}

int main(void)
{
    static const uint32_t idle[] = {0, 0x500, 0x508};
    static const uint32_t busy[] = {0x519, 0x598, 0x528, 0x108, 0x208, 0x308, UINT32_MAX};

    for (size_t index = 0; index < sizeof(idle) / sizeof(idle[0]); ++index)
    {
        check_state(idle[index], LSD_OK);
    }

    for (size_t index = 0; index < sizeof(busy) / sizeof(busy[0]); ++index)
    {
        check_state(busy[index], LSD_FAULT);
    }

    struct dma_model model = {.state = 0x519, .busy_queries = 3};
    struct ls2k3000_mmc controller =
    {
        .io = &model_io,
        .context = &model,
        .dma = &model.storage,
        .dma_address = 0x07e00000,
    };

    assert(ls2k3000_mmc_quiesce(&controller) == LSD_OK);
    assert(model.queries == 5 && model.clock == 5);

    memset(&model, 0, sizeof(model));
    model.query_stuck = 1;
    assert(ls2k3000_mmc_quiesce(&controller) == LSD_FAULT);
    assert(controller.fault && model.clock == 21);

    memset(&model, 0, sizeof(model));
    controller.fault = 0;
    controller.clock = 400000;
    controller.width = 1;
    model.card_status = 0xb00; /* READY, but still in DATA, not TRAN. */
    model.ready_after = 3;
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 10) == LSD_OK);
    assert(model.commands == 4 && model.clock == 3);
    assert(model.registers[LSD_ARG / 4] == (7U << 16));

    memset(&model, 0, sizeof(model));
    model.card_status = 0xf00; /* READY alone does not complete PRG. */
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 10) == LSD_TIMEOUT);
    assert(model.clock == 10 && model.commands == 10);

    memset(&model, 0, sizeof(model));
    model.card_status = 0x80000900U;
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 10) == LSD_IO);

    memset(&model, 0, sizeof(model));
    model.command_timeout = 1;
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 10) == LSD_TIMEOUT);
    assert(ls2k3000_mmc_wait_ready(&controller, 0, 10) == LSD_INVALID);
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 0) == LSD_INVALID);
    assert(ls2k3000_mmc_wait_ready(&controller, 7, 10001) == LSD_INVALID);
    puts("LS2K3000 MMC DMA quiescence: PASS");
    puts("LS2K3000 MMC card readiness: PASS");
    return 0;
}
