/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-25     OpenLANPU Contributors    add bounded high-address USB host support
 *
 */

/*
 * Description:
 *   Builds physical EHCI schedules with fixed 16 KiB bounce storage.
 * Notes:
 *   No descriptor is changed until schedule-status acknowledges retirement.
 *   A controller that cannot stop is quarantined until reboot; its arena is
 *   permanent, so late DMA cannot reach a caller's buffer or recycled memory.
 */

#include "ls2k_ehci.h"
#include "usb_errno.h"
#include <string.h>

_Static_assert(sizeof(struct ls2k_usb_qtd) == 64, "qTD stride");
_Static_assert(sizeof(struct ls2k_usb_qh) == 128, "QH stride");

static uint32_t read_reg(struct ls2k_ehci *hc, unsigned int reg)
{
    return hc->io->read(hc->ctx, reg);
}

static void write_reg(struct ls2k_ehci *hc, unsigned int reg, uint32_t value)
{
    hc->io->barrier(hc->ctx);
    hc->io->write(hc->ctx, reg, value);
    hc->io->barrier(hc->ctx);
}

static int wait_reg(struct ls2k_ehci *hc, unsigned int reg,
                    uint32_t mask, uint32_t wanted, uint32_t timeout)
{
    uint32_t start = hc->io->time_ms(hc->ctx);

    do
    {
        if ((read_reg(hc, reg) & mask) == wanted)
        {
            return 0;
        }

        hc->io->wait_ms(hc->ctx, 1);
    } while ((uint32_t)(hc->io->time_ms(hc->ctx) - start) < timeout);

    return -USB_ERR_TIMEOUT;
}

static uint32_t dma_at(struct ls2k_ehci *hc, size_t offset)
{
    return hc->dma_address + (uint32_t)offset;
}

static uint32_t td_address(struct ls2k_ehci *hc, unsigned int index)
{
    return dma_at(hc, offsetof(struct ls2k_usb_dma, td) +
                  index * sizeof(struct ls2k_usb_qtd));
}

int ls2k_ehci_stop(struct ls2k_ehci *hc)
{
    int ret;

    write_reg(hc, LUSB_INTR, 0);
    write_reg(hc, LUSB_CMD, 0);
    ret = wait_reg(hc, LUSB_STS, LUSB_HALTED | LUSB_ASS | LUSB_PSS,
                   LUSB_HALTED, 100);
    hc->ready = false;
    if (ret)
    {
        hc->quarantined = true;
    }

    return ret;
}

int ls2k_ehci_init(struct ls2k_ehci *hc, uint64_t dma_address)
{
    int ret;

    if (!hc || !hc->dma || !hc->io || hc->quarantined ||
        (dma_address & 4095) || dma_address < 0x200000 ||
        dma_address > UINT32_MAX - sizeof(*hc->dma))
    {
        return -USB_ERR_INVAL;
    }

    ret = ls2k_ehci_stop(hc);
    if (ret)
    {
        return ret;
    }

    write_reg(hc, LUSB_CMD, LUSB_RESET);
    ret = wait_reg(hc, LUSB_CMD, LUSB_RESET, 0, 100);
    if (ret)
    {
        hc->quarantined = true;
        return ret;
    }

    hc->dma_address = (uint32_t)dma_address;
    memset(hc->dma, 0, sizeof(*hc->dma));
    write_reg(hc, LUSB_SEGMENT, 0);
    write_reg(hc, LUSB_STS, 0x3f);
    write_reg(hc, LUSB_CONFIG, 1);
    write_reg(hc, LUSB_CMD, LUSB_RUN | (8U << 16));
    ret = wait_reg(hc, LUSB_STS, LUSB_HALTED, 0, 100);
    if (ret)
    {
        ls2k_ehci_stop(hc);
        return ret;
    }

    hc->ready = true;
    return 0;
}

static void fill_td(struct ls2k_ehci *hc, unsigned int index,
                    uint32_t address, uint32_t length, uint32_t token)
{
    struct ls2k_usb_qtd *td = &hc->dma->td[index];

    memset(td, 0, sizeof(*td));
    td->next = 1;
    td->alternate = 1;
    td->token = token | LUSB_ACTIVE | LUSB_IOC | (3U << 10) | (length << 16);
    td->buffer[0] = address;
    for (unsigned int i = 1; i < 5; ++i)
    {
        td->buffer[i] = (address & ~4095U) + i * 4096;
    }
}

static int retire(struct ls2k_ehci *hc)
{
    write_reg(hc, LUSB_CMD, read_reg(hc, LUSB_CMD) & ~(LUSB_ASE | LUSB_PSE));
    if (wait_reg(hc, LUSB_STS, LUSB_ASS | LUSB_PSS, 0, 100))
    {
        ls2k_ehci_stop(hc);
        /* Even if a full halt succeeds, require a fresh controller init. */
        return -USB_ERR_IO;
    }

    hc->io->barrier(hc->ctx);
    return 0;
}

static int transfer_chunk(struct ls2k_ehci *hc, struct ls2k_usb_transfer *x,
                          uint32_t length, uint32_t deadline_start)
{
    struct ls2k_usb_dma *dma = hc->dma;
    struct ls2k_usb_qh *qh = &dma->queue;
    unsigned int data = x->setup ? 1 : 0;
    unsigned int last = x->setup ? 2 : 0;
    uint32_t queue = dma_at(hc, offsetof(struct ls2k_usb_dma, queue));
    uint32_t token, remaining;
    int ret = 0;

    memset(&dma->head, 0, sizeof(dma->head));
    memset(qh, 0, sizeof(*qh));
    dma->head.link = queue | 2;
    dma->head.endpoint = (1U << 15) | (2U << 12);
    dma->head.next = 1;
    dma->head.alternate = 1;
    dma->head.token = LUSB_HALT;
    qh->link = hc->dma_address | 2;
    qh->endpoint = x->address | ((uint32_t)x->endpoint << 8) |
                   (2U << 12) | (1U << 14) |
                   ((uint32_t)x->max_packet << 16) | (4U << 28);
    qh->capabilities = (1U << 30);
    qh->next = td_address(hc, 0);
    qh->alternate = 1;

    if (!x->input && length)
    {
        memcpy(dma->buffer, x->buffer + x->actual, length);
    }

    fill_td(hc, data, dma_at(hc, offsetof(struct ls2k_usb_dma, buffer)),
            length, (x->input ? (1U << 8) : 0) |
            ((x->setup || x->toggle) ? LUSB_TOGGLE : 0));

    if (x->setup)
    {
        memcpy(dma->setup, x->setup, 8);
        fill_td(hc, 0, dma_at(hc, offsetof(struct ls2k_usb_dma, setup)), 8, 2U << 8);
        fill_td(hc, 2, 0, 0, LUSB_TOGGLE |
                ((!length || !x->input) ? (1U << 8) : 0));
        dma->td[0].next = td_address(hc, length ? 1 : 2);
        dma->td[1].next = td_address(hc, 2);
        /* A short control IN must still execute its status transaction. */
        dma->td[1].alternate = td_address(hc, 2);
    }

    if (x->interrupt)
    {
        qh->link = 1;
        qh->endpoint &= ~(15U << 28);
        uint32_t interval = x->interval_uframes;
        uint32_t frames = interval >= 8 ? interval / 8 : 1;
        uint32_t phase = ((read_reg(hc, LUSB_FRAME) >> 3) + 1) % frames;

        qh->capabilities |= interval == 1 ? 0xff :
                            (interval == 2 ? 0x55 : (interval == 4 ? 0x11 : 1));
        for (unsigned int i = 0; i < 1024; ++i)
        {
            dma->frames[i] = i % frames == phase ? (queue | 2) : 1;
        }

        write_reg(hc, LUSB_PERIODIC, dma_at(hc, offsetof(struct ls2k_usb_dma, frames)));
        write_reg(hc, LUSB_CMD, read_reg(hc, LUSB_CMD) | LUSB_PSE);
    }
    else
    {
        write_reg(hc, LUSB_ASYNC, hc->dma_address);
        write_reg(hc, LUSB_CMD, read_reg(hc, LUSB_CMD) | LUSB_ASE);
    }

    for (;;)
    {
        hc->io->barrier(hc->ctx);
        token = dma->td[last].token;
        for (unsigned int i = 0; i <= last; ++i)
        {
            uint32_t state = dma->td[i].token;

            if (x->setup && !length && i == 1)
            {
                continue;
            }

            if (state & 0x7cU)
            {
                ret = (state & (1U << 4)) ? -USB_ERR_BABBLE :
                      ((state & 0x3cU) ? -USB_ERR_IO : -USB_ERR_STALL);
                break;
            }
        }

        if (read_reg(hc, LUSB_STS) & LUSB_FATAL)
        {
            ret = -USB_ERR_IO;
            break;
        }

        if (ret || !(token & LUSB_ACTIVE))
        {
            break;
        }

        if (!(read_reg(hc, LUSB_PORT) & 1))
        {
            ret = -USB_ERR_NOTCONN;
            break;
        }

        if (hc->io->cancelled && hc->io->cancelled(hc->ctx))
        {
            ret = -USB_ERR_SHUTDOWN;
            break;
        }

        if ((uint32_t)(hc->io->time_ms(hc->ctx) - deadline_start) >= x->timeout_ms)
        {
            ret = -USB_ERR_TIMEOUT;
            break;
        }

        hc->io->wait_ms(hc->ctx, 1);
    }

    if (retire(hc))
    {
        return -USB_ERR_IO;
    }

    /* Completion can race the timeout and schedule disable. Reconcile only
     * after hardware retirement, otherwise a polled hub endpoint can lose a
     * packet and reuse the wrong data toggle on the next attempt. */
    if (ret == -USB_ERR_TIMEOUT && !(dma->td[last].token & LUSB_ACTIVE))
    {
        ret = 0;
        for (unsigned int i = 0; i <= last; ++i)
        {
            if (dma->td[i].token & 0x7cU)
            {
                ret = -USB_ERR_IO;
                break;
            }
        }
    }

    if (read_reg(hc, LUSB_STS) & LUSB_FATAL)
    {
        ret = -USB_ERR_IO;
    }

    if (ret)
    {
        if (read_reg(hc, LUSB_STS) & LUSB_FATAL)
        {
            ls2k_ehci_stop(hc);
        }

        return ret;
    }

    remaining = (dma->td[data].token >> 16) & 0x7fff;
    if (!length)
    {
        remaining = 0;
    }

    if (remaining > length)
    {
        return -USB_ERR_IO;
    }

    if (x->input && length != remaining)
    {
        memcpy(x->buffer + x->actual, dma->buffer, length - remaining);
    }

    x->actual += length - remaining;
    x->toggle = (qh->token & LUSB_TOGGLE) != 0;
    return remaining ? 1 : 0;
}

int ls2k_ehci_transfer(struct ls2k_ehci *hc, struct ls2k_usb_transfer *x)
{
    uint32_t start, length;
    int ret;

    if (!hc || !x || !hc->ready || hc->quarantined)
    {
        return -USB_ERR_NODEV;
    }

    if (x->address > 127 || x->endpoint > 15 || !x->max_packet ||
        x->max_packet > 1024 || !x->timeout_ms || x->timeout_ms > 60000 ||
        (x->length && !x->buffer) || (x->setup && x->length > LUSB_BUFFER_SIZE) ||
        (x->interrupt && (x->setup || x->length > x->max_packet ||
         !x->interval_uframes || x->interval_uframes > 8192 ||
         (x->interval_uframes & (x->interval_uframes - 1)))) ||
        (!x->interrupt && LUSB_BUFFER_SIZE % x->max_packet))
    {
        return -USB_ERR_INVAL;
    }

    x->actual = 0;
    start = hc->io->time_ms(hc->ctx);
    do
    {
        length = x->length - x->actual;
        if (length > LUSB_BUFFER_SIZE)
        {
            length = LUSB_BUFFER_SIZE;
        }

        ret = transfer_chunk(hc, x, length, start);
        if (ret)
        {
            return ret < 0 ? ret : 0;
        }
    } while (x->actual < x->length);

    return 0;
}
