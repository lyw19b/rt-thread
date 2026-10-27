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
 *   Native NVMe admin and NVM commands, independent of firmware block services.
 */

#include "nvme_poll.h"
#include <string.h>

#define NVME_CAP 0x00U
#define NVME_INTMS 0x0cU
#define NVME_CC 0x14U
#define NVME_CSTS 0x1cU
#define NVME_AQA 0x24U
#define NVME_ASQ 0x28U
#define NVME_ACQ 0x30U
#define NVME_DOORBELL 0x1000U

static uint32_t read32(struct ls_nvme *controller, uint32_t offset)
{
    return controller->ops->read32(controller->context, offset);
}

static void write32(struct ls_nvme *controller, uint32_t offset, uint32_t value)
{
    controller->ops->write32(controller->context, offset, value);
}

static void barrier(struct ls_nvme *controller)
{
    controller->ops->barrier(controller->context);
}

static uint32_t get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t get_le64(const uint8_t *bytes)
{
    return get_le32(bytes) | (uint64_t)get_le32(bytes + 4) << 32;
}

static void write64(struct ls_nvme *controller, uint32_t offset, uint64_t value)
{
    write32(controller, offset, (uint32_t)value);
    write32(controller, offset + 4, (uint32_t)(value >> 32));
}

static int wait_ready(struct ls_nvme *controller, unsigned int expected)
{
    for (unsigned int elapsed = 0; elapsed <= controller->ready_timeout_ms; ++elapsed)
    {
        uint32_t status = read32(controller, NVME_CSTS);

        if (status == UINT32_MAX || (expected && (status & 2)))
        {
            return LS_NVME_DEVICE;
        }

        if ((status & 1) == expected)
        {
            return LS_NVME_OK;
        }

        if (elapsed < controller->ready_timeout_ms)
        {
            controller->ops->sleep_ms(controller->context, 1);
        }
    }

    return LS_NVME_TIMEOUT;
}

int ls_nvme_stop(struct ls_nvme *controller)
{
    controller->online = 0;
    write32(controller, NVME_INTMS, UINT32_MAX);
    write32(controller, NVME_CC, read32(controller, NVME_CC) & ~1U);
    barrier(controller);
    return wait_ready(controller, 0);
}

static int fault(struct ls_nvme *controller, int error)
{
    controller->faulted = 1;
    /* Memory remains pinned even if RDY never clears. Never reuse stale DMA. */
    ls_nvme_stop(controller);
    return error;
}

int ls_nvme_submit(struct ls_nvme *controller, unsigned int queue_id,
                   struct ls_nvme_command *command, unsigned int timeout_ms)
{
    struct ls_nvme_queue *queue;
    volatile uint32_t *entry;
    uint16_t cid;

    if (queue_id > 1 || !command || controller->faulted || !controller->depth)
    {
        return LS_NVME_INVALID;
    }

    queue = &controller->queue[queue_id];
    cid = ++queue->cid;
    command->word[0] = (command->word[0] & 0xffffU) | (uint32_t)cid << 16;
    entry = queue->submission + queue->tail * 16;

    for (unsigned int index = 0; index < 16; ++index)
    {
        entry[index] = command->word[index];
    }

    queue->tail = (queue->tail + 1) % controller->depth;
    barrier(controller);
    write32(controller, NVME_DOORBELL + 2 * queue_id * controller->doorbell_stride,
            queue->tail);

    for (unsigned int elapsed = 0; elapsed <= timeout_ms; ++elapsed)
    {
        uint32_t completion;
        uint32_t status;

        entry = queue->completion + queue->head * 4;
        completion = entry[3];

        if (((completion >> 16) & 1) == queue->phase)
        {
            barrier(controller);

            if ((uint16_t)completion != cid || (entry[2] >> 16) != queue_id ||
                (entry[2] & 0xffff) >= controller->depth)
            {
                return fault(controller, LS_NVME_DEVICE);
            }

            controller->last_status = completion >> 17;
            queue->head++;

            if (queue->head == controller->depth)
            {
                queue->head = 0;
                queue->phase ^= 1;
            }

            barrier(controller);
            write32(controller, NVME_DOORBELL + (2 * queue_id + 1) *
                    controller->doorbell_stride, queue->head);
            return controller->last_status ? LS_NVME_DEVICE : LS_NVME_OK;
        }

        status = read32(controller, NVME_CSTS);

        if (status == UINT32_MAX || (status & 3) != 1)
        {
            return fault(controller, LS_NVME_DEVICE);
        }

        if (elapsed < timeout_ms)
        {
            controller->ops->sleep_ms(controller->context, 1);
        }
    }

    return fault(controller, LS_NVME_TIMEOUT);
}

static int identify(struct ls_nvme *controller, uint32_t namespace_id, uint32_t cns)
{
    struct ls_nvme_command command = {0};

    memset(controller->bounce, 0, LS_NVME_PAGE);
    command.word[0] = 6;
    command.word[1] = namespace_id;
    command.word[6] = (uint32_t)controller->bounce_dma;
    command.word[7] = (uint32_t)(controller->bounce_dma >> 32);
    command.word[10] = cns;
    return ls_nvme_submit(controller, 0, &command, 10000);
}

static int discover_namespace(struct ls_nvme *controller)
{
    unsigned int format;
    const uint8_t *lbaf;
    int result;

    result = identify(controller, 0, 1);

    if (result)
    {
        return result;
    }

    /* SQES/CQES encode minimum and maximum supported entry size exponents. */
    if ((controller->bounce[512] & 15) > 6 || (controller->bounce[512] >> 4) < 6 ||
        (controller->bounce[513] & 15) > 4 || (controller->bounce[513] >> 4) < 4)
    {
        return LS_NVME_UNSUPPORTED;
    }

    result = identify(controller, 0, 2);

    if (result)
    {
        return result;
    }

    controller->namespace_id = get_le32(controller->bounce);

    if (!controller->namespace_id || controller->namespace_id == UINT32_MAX)
    {
        return LS_NVME_UNSUPPORTED;
    }

    result = identify(controller, controller->namespace_id, 0);

    if (result)
    {
        return result;
    }

    format = controller->bounce[26] & 15;
    lbaf = controller->bounce + 128 + 4 * format;

    /* Reject metadata, protection information and extended LBA formats. */
    if ((controller->bounce[26] & 0xf0) || format > controller->bounce[25] ||
        (controller->bounce[29] & 7) || lbaf[0] || lbaf[1] ||
        (lbaf[2] != 9 && lbaf[2] != 12))
    {
        return LS_NVME_UNSUPPORTED;
    }

    controller->sectors = get_le64(controller->bounce);
    controller->sector_bytes = 1U << lbaf[2];
    return controller->sectors ? LS_NVME_OK : LS_NVME_UNSUPPORTED;
}

int ls_nvme_start(struct ls_nvme *controller)
{
    uint64_t cap;
    uint32_t maximum;
    struct ls_nvme_command command = {0};
    int result;

    if (!controller || !controller->ops || !controller->ops->read32 ||
        !controller->ops->write32 || !controller->ops->barrier ||
        !controller->ops->sleep_ms || controller->faulted || controller->online ||
        !controller->bounce || (controller->bounce_dma & (LS_NVME_PAGE - 1)) ||
        !controller->bounce_dma || controller->bar_bytes < NVME_DOORBELL + 16)
    {
        return LS_NVME_INVALID;
    }

    for (unsigned int index = 0; index < 2; ++index)
    {
        struct ls_nvme_queue *queue = &controller->queue[index];

        if (!queue->submission || !queue->completion || !queue->submission_dma ||
            !queue->completion_dma ||
            ((queue->submission_dma | queue->completion_dma) & (LS_NVME_PAGE - 1)))
        {
            return LS_NVME_INVALID;
        }
    }

    cap = read32(controller, NVME_CAP);
    cap |= (uint64_t)read32(controller, NVME_CAP + 4) << 32;
    maximum = (cap & 0xffff) + 1;
    controller->doorbell_stride = 4U << ((cap >> 32) & 15);
    controller->ready_timeout_ms = (((cap >> 24) & 255) + 1) * 500;

    if (cap == UINT64_MAX || maximum < 2 || !(cap & (UINT64_C(1) << 37)) ||
        ((cap >> 48) & 15) || controller->bar_bytes < NVME_DOORBELL +
        3 * (uint64_t)controller->doorbell_stride + 4)
    {
        return LS_NVME_UNSUPPORTED;
    }

    controller->depth = maximum < LS_NVME_DEPTH ? maximum : LS_NVME_DEPTH;
    result = ls_nvme_stop(controller);

    if (result)
    {
        controller->faulted = 1;
        return result;
    }

    for (unsigned int index = 0; index < 2; ++index)
    {
        struct ls_nvme_queue *queue = &controller->queue[index];

        memset((void *)queue->submission, 0, LS_NVME_PAGE);
        memset((void *)queue->completion, 0, LS_NVME_PAGE);
        queue->tail = 0;
        queue->head = 0;
        queue->cid = 0;
        queue->phase = 1;
        queue->id = index;
    }

    write32(controller, NVME_AQA, (controller->depth - 1) * 0x10001U);
    write64(controller, NVME_ASQ, controller->queue[0].submission_dma);
    write64(controller, NVME_ACQ, controller->queue[0].completion_dma);
    barrier(controller);
    write32(controller, NVME_CC, 1 | (6 << 16) | (4 << 20));
    result = wait_ready(controller, 1);

    if (result)
    {
        return fault(controller, result);
    }

    result = discover_namespace(controller);

    if (result)
    {
        return fault(controller, result);
    }

    /* Allocate one I/O queue pair before creating it (Number of Queues feature). */
    command.word[0] = 9;
    command.word[10] = 7;
    result = ls_nvme_submit(controller, 0, &command, 10000);

    if (result)
    {
        return fault(controller, result);
    }

    memset(&command, 0, sizeof(command));
    command.word[0] = 5;
    command.word[6] = (uint32_t)controller->queue[1].completion_dma;
    command.word[7] = (uint32_t)(controller->queue[1].completion_dma >> 32);
    command.word[10] = 1 | (uint32_t)(controller->depth - 1) << 16;
    command.word[11] = 1;
    result = ls_nvme_submit(controller, 0, &command, 10000);

    if (result)
    {
        return fault(controller, result);
    }

    command.word[0] = 1;
    command.word[6] = (uint32_t)controller->queue[1].submission_dma;
    command.word[7] = (uint32_t)(controller->queue[1].submission_dma >> 32);
    command.word[11] = 1 | (1 << 16);
    result = ls_nvme_submit(controller, 0, &command, 10000);

    if (result)
    {
        return fault(controller, result);
    }

    controller->online = 1;
    return LS_NVME_OK;
}

int ls_nvme_transfer(struct ls_nvme *controller, uint64_t lba, void *buffer, int write)
{
    struct ls_nvme_command command = {0};
    int result;

    if (!controller->online || !buffer || lba >= controller->sectors)
    {
        return LS_NVME_INVALID;
    }

    if (write)
    {
        memcpy(controller->bounce, buffer, controller->sector_bytes);
    }

    command.word[0] = write ? 1 : 2;
    command.word[1] = controller->namespace_id;
    command.word[6] = (uint32_t)controller->bounce_dma;
    command.word[7] = (uint32_t)(controller->bounce_dma >> 32);
    command.word[10] = (uint32_t)lba;
    command.word[11] = (uint32_t)(lba >> 32);
    /* One LBA per command; FUA makes an enabled write durable on completion. */
    command.word[12] = write ? 1U << 30 : 0;
    result = ls_nvme_submit(controller, 1, &command, 10000);

    if (!result && !write)
    {
        memcpy(buffer, controller->bounce, controller->sector_bytes);
    }

    return result;
}

int ls_nvme_flush(struct ls_nvme *controller)
{
    struct ls_nvme_command command = {0};

    if (!controller->online)
    {
        return LS_NVME_INVALID;
    }

    command.word[1] = controller->namespace_id;
    return ls_nvme_submit(controller, 1, &command, 10000);
}
