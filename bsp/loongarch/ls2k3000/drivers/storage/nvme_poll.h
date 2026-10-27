/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: nvme_poll.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Serialized NVMe queues with caller-owned, permanently pinned DMA memory.
 */

#ifndef LS2K3000_NVME_POLL_H
#define LS2K3000_NVME_POLL_H

#include <stdint.h>
#include <stddef.h>

#define LS_NVME_PAGE 4096U
#define LS_NVME_DEPTH 16U
#define LS_NVME_OK 0
#define LS_NVME_INVALID (-1)
#define LS_NVME_TIMEOUT (-2)
#define LS_NVME_DEVICE (-3)
#define LS_NVME_UNSUPPORTED (-4)

struct ls_nvme_ops
{
    uint32_t (*read32)(void *context, uint32_t offset);
    void (*write32)(void *context, uint32_t offset, uint32_t value);
    void (*barrier)(void *context);
    void (*sleep_ms)(void *context, unsigned int milliseconds);
};

struct ls_nvme_command
{
    uint32_t word[16];
};

struct ls_nvme_queue
{
    volatile uint32_t *submission;
    volatile uint32_t *completion;
    uint64_t submission_dma;
    uint64_t completion_dma;
    uint16_t tail;
    uint16_t head;
    uint16_t cid;
    uint8_t phase;
    uint8_t id;
};

struct ls_nvme
{
    const struct ls_nvme_ops *ops;
    void *context;
    struct ls_nvme_queue queue[2];
    uint8_t *bounce;
    uint64_t bounce_dma;
    uint64_t bar_bytes;
    uint64_t sectors;
    uint32_t sector_bytes;
    uint32_t namespace_id;
    uint32_t doorbell_stride;
    unsigned int ready_timeout_ms;
    uint16_t depth;
    uint16_t last_status;
    int faulted;
    int online;
};

/* The caller serializes all operations; no IRQ handler may touch these queues. */
int ls_nvme_start(struct ls_nvme *controller);
int ls_nvme_stop(struct ls_nvme *controller);
int ls_nvme_submit(struct ls_nvme *controller, unsigned int queue,
                   struct ls_nvme_command *command, unsigned int timeout_ms);
int ls_nvme_transfer(struct ls_nvme *controller, uint64_t lba, void *buffer, int write);
int ls_nvme_flush(struct ls_nvme *controller);

#endif
