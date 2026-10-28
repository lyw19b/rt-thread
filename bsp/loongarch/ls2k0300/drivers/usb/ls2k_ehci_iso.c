/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     LoongsonLab               add board-owned USB camera support
 *
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 * 2026-10-28     LoongsonLab               keep empty completions out of payload queue
 * 2026-10-28     LoongsonLab               publish independent ISO schedules before enable
 */

/*
 * Description:
 *   Keeps 32 iTDs ahead of FRINDEX, including high-bandwidth transactions.
 * Notes:
 *   The HCD lock serializes this module. Poll also runs during synchronous
 *   USB waits, so an unrelated control transfer cannot starve the schedule.
 *   Only the fixed arena is DMA-visible; loss poisons the next UVC frame.
 */

#include "ls2k_ehci_iso.h"
#include "usb_errno.h"
#include <string.h>

_Static_assert(sizeof(struct ls2k_itd) == 96, "extended iTD stride");
_Static_assert(sizeof(((struct ls2k_iso_dma *)0)->frames) == 4096, "ISO frame-list size");
_Static_assert(offsetof(struct ls2k_iso_dma, frames) % 4096 == 0, "ISO frame-list alignment");

/* A start succeeds only with at least half a millisecond left before its
 * first scheduled microframe. This is a driver contract, not an inference
 * about the controller's undocumented internal buffering. */
#define LUSB_ISO_START_MARGIN_UFRAMES 4U

static uint16_t current_frame(struct ls2k_ehci *hc)
{
    return (hc->io->read(hc->ctx, LUSB_FRAME) >> 3) & 2047;
}

static uint32_t tail(struct ls2k_iso *s, unsigned frame)
{
    return s->tail_interval && frame % s->tail_interval == s->tail_phase ? s->tail_link : 1;
}

static void schedule(struct ls2k_ehci *hc, unsigned index, uint16_t frame)
{
    struct ls2k_iso *s = hc->iso;
    struct ls2k_itd *d = &s->dma->itd[index];
    uint32_t buffer = s->physical + offsetof(struct ls2k_iso_dma, data) +
                      index * sizeof(s->dma->data[0]);

    memset(d, 0, sizeof(*d));
    d->next = tail(s, frame & 1023);
    for (unsigned i = 0; i < 7; ++i)
    {
        d->buffer[i] = buffer + i * 4096;
    }

    d->buffer[0] |= s->address | ((uint32_t)s->endpoint << 8);
    d->buffer[1] |= (s->packet & 0x7ff) | (1U << 11);
    d->buffer[2] |= 1 + ((s->packet >> 11) & 3);
    for (unsigned u = 0; u < 8; u += s->interval)
    {
        uint32_t offset = u * s->capacity;

        d->transaction[u] = (1U << 31) | ((uint32_t)s->capacity << 16) | ((offset >> 12) << 12) |
                            (offset & 4095);
    }

    s->frame[index] = frame;
    hc->io->barrier(hc->ctx);
    s->dma->frames[frame & 1023] = s->physical + index * sizeof(*d);
    hc->io->barrier(hc->ctx);
}

void ls2k_iso_tail(struct ls2k_ehci *hc, uint32_t link, uint32_t interval, uint32_t phase)
{
    struct ls2k_iso *s = hc->iso;

    s->tail_interval = interval;
    s->tail_phase = phase;
    s->tail_link = link;
    /* Publish fully initialized QH/qTD fields before linking a live schedule. */
    hc->io->barrier(hc->ctx);
    for (unsigned i = 0; i < 1024; ++i)
    {
        /* Preserve every live iTD head while changing its following QH. */
        if (s->dma->frames[i] & 1 || (s->dma->frames[i] & 6) == 2)
        {
            s->dma->frames[i] = tail(s, i);
        }
    }

    for (unsigned i = 0; i < LUSB_ISO_SLOTS; ++i)
    {
        s->dma->itd[i].next = tail(s, s->frame[i] & 1023);
    }

    hc->io->barrier(hc->ctx);
}

int ls2k_iso_start(struct ls2k_ehci *hc, struct ls2k_iso *s, uint64_t physical, uint8_t address,
                   uint8_t endpoint, uint16_t packet, uint8_t interval)
{
    uint16_t frame;
    uint32_t start, prepared_at;

    if (!hc->ready || hc->quarantined || !s || !s->dma || hc->iso || !address || address > 127 ||
        !endpoint || endpoint > 15 || !(packet & 0x7ff) || (packet & 0x7ff) > 1024 ||
        (packet & 0xe000) || ((packet >> 11) & 3) == 3 || !interval || interval > 4 ||
        (physical & 4095) || physical < 0x200000 || physical > UINT32_MAX - sizeof(*s->dma))
    {
        return -USB_ERR_INVAL;
    }

    /* The caller holds the HCD lock. Do not replace a periodic table still
     * owned by an interrupt transfer or a previous unconfirmed shutdown. */
    if ((hc->io->read(hc->ctx, LUSB_CMD) & LUSB_PSE) ||
        (hc->io->read(hc->ctx, LUSB_STS) & LUSB_PSS))
    {
        return -USB_ERR_BUSY;
    }

    s->running = false;
    s->physical = physical;
    s->packet = packet;
    s->capacity = (packet & 0x7ff) * (1 + ((packet >> 11) & 3));
    s->interval = 1U << (interval - 1);
    s->address = address;
    s->endpoint = endpoint;
    s->cursor = 0;
    s->head = 0;
    s->count = 0;
    s->dropped = 0;
    s->missed_transactions = 0;
    s->transaction_errors = 0;
    s->late_reschedules = 0;
    s->max_poll_gap = 0;
    s->empty_transactions = 0;
    s->lost = false;
    s->tail_interval = 0;
    s->max_read_gap = 0;
    s->max_queued = 0;
    hc->iso = s;
    for (unsigned i = 0; i < 1024; ++i)
    {
        s->dma->frames[i] = 1;
    }

    /* On LS2K0300, both reusing the standalone Hub table and publishing
     * links after enabling an empty table skipped the first scheduled frames
     * in board tests. Use a separate, completely prepared ISO table before
     * PSE. The tests do not establish the internal buffering mechanism.
     * Keep polling disabled until the handshake and deadline checks finish;
     * wait_ms may re-enter ls2k_iso_poll. */
    prepared_at = hc->io->time_ms(hc->ctx);
    frame = (current_frame(hc) + 4) & 2047;
    for (unsigned i = 0; i < LUSB_ISO_SLOTS; ++i)
    {
        schedule(hc, i, (frame + i) & 2047);
    }

    s->next_frame = (frame + LUSB_ISO_SLOTS) & 2047;

    hc->io->write(hc->ctx, LUSB_PERIODIC, s->physical + offsetof(struct ls2k_iso_dma, frames));
    hc->io->barrier(hc->ctx);
    hc->io->write(hc->ctx, LUSB_CMD, hc->io->read(hc->ctx, LUSB_CMD) | LUSB_PSE);
    hc->io->barrier(hc->ctx);
    start = hc->io->time_ms(hc->ctx);

    while (!(hc->io->read(hc->ctx, LUSB_STS) & LUSB_PSS))
    {
        if ((uint32_t)(hc->io->time_ms(hc->ctx) - start) >= 100)
        {
            /* Preserve the permanent arena if shutdown cannot be confirmed.
             * stop performs full controller halt/quarantine when necessary. */
            return ls2k_iso_stop(hc) ? -USB_ERR_IO : -USB_ERR_TIMEOUT;
        }

        hc->io->wait_ms(hc->ctx, 1);
    }

    /* The initial +4-frame target is at most 32 microframes ahead. This
     * upper bound also rejects a target already passed across FRINDEX wrap.
     * The independent elapsed-time bound rejects a long preemption spanning
     * a complete FRINDEX wrap that would otherwise look like a future slot.
     * A late enable is a failed start; do not silently lose initial packets
     * or rewrite a schedule already owned by the controller. */
    uint32_t first_uframe = (uint32_t)s->frame[0] << 3;
    uint32_t ahead = (first_uframe - hc->io->read(hc->ctx, LUSB_FRAME)) & 0x3fffU;

    if ((uint32_t)(hc->io->time_ms(hc->ctx) - prepared_at) >= 4 ||
        ahead < LUSB_ISO_START_MARGIN_UFRAMES || ahead > 32)
    {
        return ls2k_iso_stop(hc) ? -USB_ERR_IO : -USB_ERR_TIMEOUT;
    }

    s->last_poll = hc->io->time_ms(hc->ctx);
    s->last_read = s->last_poll;
    s->running = true;
    return 0;
}

int ls2k_iso_stop(struct ls2k_ehci *hc)
{
    struct ls2k_iso *s = hc->iso;
    uint32_t start;

    if (!s)
    {
        return 0;
    }

    /* Mark stopped before wait_ms, whose hook can poll this module. */
    s->running = false;
    hc->io->write(hc->ctx, LUSB_CMD, hc->io->read(hc->ctx, LUSB_CMD) & ~LUSB_PSE);
    hc->io->barrier(hc->ctx);
    start = hc->io->time_ms(hc->ctx);
    while (hc->io->read(hc->ctx, LUSB_STS) & LUSB_PSS)
    {
        if ((uint32_t)(hc->io->time_ms(hc->ctx) - start) >= 100)
        {
            ls2k_ehci_stop(hc);
            hc->iso = NULL;
            return -USB_ERR_IO;
        }

        hc->io->wait_ms(hc->ctx, 1);
    }

    hc->io->barrier(hc->ctx);
    hc->iso = NULL;
    return 0;
}

void ls2k_iso_poll(struct ls2k_ehci *hc)
{
    struct ls2k_iso *s = hc->iso;
    uint16_t now;
    uint32_t clock;

    if (!s || !s->running)
    {
        return;
    }

    clock = hc->io->time_ms(hc->ctx);
    if ((uint32_t)(clock - s->last_poll) > s->max_poll_gap)
    {
        s->max_poll_gap = clock - s->last_poll;
    }

    if (!hc->ready || (hc->io->read(hc->ctx, LUSB_STS) & LUSB_FATAL) ||
        !(hc->io->read(hc->ctx, LUSB_PORT) & 1) || (uint32_t)(clock - s->last_poll) >= 512)
    {
        /* Do not recycle a possibly prefetched descriptor after a long stall. */
        s->running = false;
        s->lost = true;
        return;
    }

    s->last_poll = clock;
    now = current_frame(hc);
    hc->io->barrier(hc->ctx);
    for (unsigned n = 0; n < LUSB_ISO_SLOTS; ++n)
    {
        unsigned i = s->cursor;
        unsigned age = (now - s->frame[i]) & 2047;
        struct ls2k_itd *d = &s->dma->itd[i];

        if (age < 2 || age >= 1024)
        {
            break;
        }

        s->dma->frames[s->frame[i] & 1023] = tail(s, s->frame[i] & 1023);
        for (unsigned u = 0; u < 8; u += s->interval)
        {
            uint32_t status = d->transaction[u];
            unsigned length = (status >> 16) & 0xfff;
            struct ls2k_iso_packet *p;

            s->missed_transactions += !!(status & (1U << 31));
            s->transaction_errors += !!(status & 0x70000000U);

            /* Successful zero-byte microframes carry no UVC header or data.
             * Do not consume queue slots between camera images. Keep every
             * error, including a zero-byte error, visible to the assembler. */
            if (!(status & 0xf0000000U) && !length)
            {
                ++s->empty_transactions;
                continue;
            }

            if (s->count == LUSB_ISO_QUEUE)
            {
                ++s->dropped;
                s->lost = true;
                continue;
            }

            p = &s->queue[(s->head + s->count++) % LUSB_ISO_QUEUE];

            if (s->count > s->max_queued)
            {
                s->max_queued = s->count;
            }

            p->error = (status & 0xf0000000U) || length > s->capacity ? -USB_ERR_IO : 0;
            p->length = p->error ? 0 : length;
            if (p->length)
            {
                hc->io->barrier(hc->ctx);
                memcpy(p->data, &s->dma->data[i][u * s->capacity], p->length);
            }
        }

        if (((s->next_frame - now) & 2047) < 4 || ((s->next_frame - now) & 2047) >= 1024)
        {
            s->next_frame = (now + 4) & 2047;
            s->lost = true;
            ++s->late_reschedules;
        }

        schedule(hc, i, s->next_frame);
        s->next_frame = (s->next_frame + 1) & 2047;
        s->cursor = (i + 1) % LUSB_ISO_SLOTS;
    }
}

int ls2k_iso_read(struct ls2k_ehci *hc, uint8_t *data, size_t capacity)
{
    struct ls2k_iso *s = hc->iso;
    struct ls2k_iso_packet *p;
    int result;
    uint32_t clock;

    if (!s || !s->running)
    {
        return -USB_ERR_NODEV;
    }

    clock = hc->io->time_ms(hc->ctx);

    if ((uint32_t)(clock - s->last_read) > s->max_read_gap)
    {
        s->max_read_gap = clock - s->last_read;
    }

    s->last_read = clock;

    if (s->lost)
    {
        s->lost = false;
        /* Drop queued pre-gap packets so the error precedes the next frame. */
        s->head = 0;
        s->count = 0;
        return -USB_ERR_IO;
    }

    if (!s->count)
    {
        return -USB_ERR_NAK;
    }

    p = &s->queue[s->head];
    if (capacity < p->length)
    {
        return -USB_ERR_INVAL;
    }

    result = p->error ? p->error : p->length;
    memcpy(data, p->data, p->length);
    s->head = (s->head + 1) % LUSB_ISO_QUEUE;
    --s->count;
    return result;
}
