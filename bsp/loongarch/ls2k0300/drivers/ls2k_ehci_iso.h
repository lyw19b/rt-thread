/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     OpenLANPU Contributors    add board-owned USB camera support
 *
 */

/* Description: One persistent high-speed IN stream with permanent bounce DMA. */
#ifndef LS2K_EHCI_ISO_H
#define LS2K_EHCI_ISO_H

#include "ls2k_ehci.h"

#define LUSB_ISO_SLOTS 32U
#define LUSB_ISO_PACKET 3072U
#define LUSB_ISO_QUEUE 128U

struct ls2k_itd
{
    volatile uint32_t next, transaction[8], buffer[7];
    uint32_t upper[7], reserved;
} __attribute__((aligned(32)));

struct ls2k_iso_dma
{
    struct ls2k_itd itd[LUSB_ISO_SLOTS];
    uint8_t data[LUSB_ISO_SLOTS][8 * LUSB_ISO_PACKET] __attribute__((aligned(4096)));
} __attribute__((aligned(4096)));

struct ls2k_iso_packet
{
    uint16_t length;
    int16_t error;
    uint8_t data[LUSB_ISO_PACKET];
};

struct ls2k_iso
{
    struct ls2k_iso_dma *dma;
    uint32_t physical, last_poll, dropped;
    uint16_t frame[LUSB_ISO_SLOTS], next_frame;
    uint16_t packet, capacity, interval;
    uint8_t address, endpoint, cursor;
    bool running, lost;
    unsigned head, count;
    uint32_t tail_interval, tail_phase, tail_link;
    struct ls2k_iso_packet queue[LUSB_ISO_QUEUE];
};

int ls2k_iso_start(struct ls2k_ehci *hc, struct ls2k_iso *iso,
                   uint64_t physical, uint8_t address, uint8_t endpoint,
                   uint16_t packet, uint8_t interval);
int ls2k_iso_stop(struct ls2k_ehci *hc);
void ls2k_iso_poll(struct ls2k_ehci *hc);
void ls2k_iso_tail(struct ls2k_ehci *hc, uint32_t link,
                    uint32_t interval, uint32_t phase);
int ls2k_iso_read(struct ls2k_ehci *hc, uint8_t *data, size_t capacity);

#endif
