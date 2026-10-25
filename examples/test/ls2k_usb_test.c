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
 *   Run the actual EHCI/BOT engines against fault-injecting host models.
 * Notes:
 *   Verifies wire layout, retirement ordering, high CPU addresses and bounded
 *   failure paths. This is not a USB PHY or device compliance simulation.
 */

#include "ls2k_ehci.h"
#include "ls2k_usb_bot.h"
#include "usb_errno.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ls2k_usb_dma storage;

struct model
{
    struct ls2k_ehci hc;
    uint32_t regs[32], time, chunks, bytes;
    bool reset_stuck, schedule_stuck, halt_stuck, timeout, disconnect, cancel;
    bool complete_on_retire;
    int error;
    uint32_t short_bytes;
    bool short_packet, control, input, periodic;
    uint32_t interval;
};

static uint32_t model_read(void *ctx, unsigned int reg)
{
    struct model *m = ctx;

    return m->regs[reg / 4];
}

static void *physical(struct model *m, uint32_t address, size_t length)
{
    assert(address >= m->hc.dma_address);
    assert(length <= sizeof(storage));
    assert((uint64_t)address + length <= (uint64_t)m->hc.dma_address + sizeof(storage));
    return (uint8_t *)&storage + address - m->hc.dma_address;
}

static void complete_schedule(struct model *m, uint32_t command)
{
    struct ls2k_usb_qh *qh = physical(m, m->hc.dma_address + 128, 128);
    unsigned int data = m->control ? 1 : 0;
    unsigned int last = m->control ? 2 : 0;
    struct ls2k_usb_qtd *td = &storage.td[data];
    uint32_t length = (td->token >> 16) & 0x7fff;
    uint32_t actual = m->short_packet ? m->short_bytes : length;
    uint32_t packet = (qh->endpoint >> 16) & 0x7ff;
    uint32_t address = storage.td[0].next;

    assert(qh->next == m->hc.dma_address + offsetof(struct ls2k_usb_dma, td));
    assert(((qh->endpoint >> 12) & 3) == 2);
    assert(qh->endpoint & (1U << 14));
    assert(actual <= length);
    assert(((command & LUSB_PSE) != 0) == m->periodic);
    if (m->periodic)
    {
        assert(m->regs[LUSB_PERIODIC / 4] % 4096 == 0);
        unsigned int step = m->interval < 8 ? 1 : m->interval / 8;
        unsigned int phase = ((m->regs[LUSB_FRAME / 4] >> 3) + 1) % step;
        unsigned int mask = m->interval == 1 ? 0xff :
                            (m->interval == 2 ? 0x55 : (m->interval == 4 ? 0x11 : 1));

        assert((qh->capabilities & 0xff) == mask);
        for (unsigned int i = 0; i < 1024; ++i)
        {
            assert(storage.frames[i] == (i % step == phase ?
                   (m->hc.dma_address + 128 + 2) : 1));
        }
    }
    else
    {
        assert(m->regs[LUSB_ASYNC / 4] == m->hc.dma_address);
        assert(storage.head.link == (m->hc.dma_address + 128 + 2));
        assert(qh->link == (m->hc.dma_address + 2));
        assert(storage.head.endpoint & (1U << 15));
    }

    if (m->control)
    {
        assert(((storage.td[0].token >> 8) & 3) == 2);
        assert((storage.td[0].token >> 16 & 0x7fff) == 8);
        assert(storage.td[2].token & LUSB_TOGGLE);
        if (length)
        {
            assert(address == m->hc.dma_address + offsetof(struct ls2k_usb_dma, td[1]));
            assert(storage.td[1].alternate ==
                   m->hc.dma_address + offsetof(struct ls2k_usb_dma, td[2]));
        }
        else
        {
            assert(address == m->hc.dma_address + offsetof(struct ls2k_usb_dma, td[2]));
        }
    }

    if (m->timeout || m->disconnect || m->cancel)
    {
        return;
    }

    if (m->error)
    {
        storage.td[0].token = m->error;
        return;
    }

    if (length)
    {
        uint8_t *buffer = physical(m, td->buffer[0], actual);

        for (unsigned int i = 0; i < actual; ++i)
        {
            uint8_t pattern = (m->bytes + i) * 37U + 11;

            if (m->input)
            {
                buffer[i] = pattern;
            }
            else
            {
                assert(buffer[i] == pattern);
            }
        }

        for (unsigned int i = 1; i < 5; ++i)
        {
            assert(td->buffer[i] == (td->buffer[0] & ~4095U) + i * 4096);
        }
    }

    for (unsigned int i = 0; i <= last; ++i)
    {
        storage.td[i].token &= ~(LUSB_ACTIVE | (0x7fffU << 16));
    }

    td->token |= (length - actual) << 16;
    qh->token = (((td->token & LUSB_TOGGLE) != 0) ^
                (((actual + packet - 1) / packet +
                  ((actual < length || !length) && actual % packet == 0)) & 1)) ?
                LUSB_TOGGLE : 0;
    m->bytes += actual;
    ++m->chunks;
}

static void model_write(void *ctx, unsigned int reg, uint32_t value)
{
    struct model *m = ctx;

    if (reg == LUSB_STS)
    {
        m->regs[reg / 4] &= ~(value & 0x3f);
        return;
    }

    m->regs[reg / 4] = value;
    if (reg != LUSB_CMD)
    {
        return;
    }

    if (value & LUSB_RESET)
    {
        if (!m->reset_stuck)
        {
            m->regs[reg / 4] &= ~LUSB_RESET;
        }
        return;
    }

    if (!(value & LUSB_RUN))
    {
        if (!m->halt_stuck)
        {
            m->regs[LUSB_STS / 4] = LUSB_HALTED;
        }
        return;
    }

    m->regs[LUSB_STS / 4] &= ~LUSB_HALTED;
    if (!(value & (LUSB_ASE | LUSB_PSE)))
    {
        if (m->complete_on_retire)
        {
            m->timeout = m->complete_on_retire = false;
            complete_schedule(m, LUSB_ASE);
        }

        if (!m->schedule_stuck)
        {
            m->regs[LUSB_STS / 4] &= ~(LUSB_ASS | LUSB_PSS);
        }
        return;
    }

    m->regs[LUSB_STS / 4] |= (value & LUSB_ASE) ? LUSB_ASS : LUSB_PSS;
    complete_schedule(m, value);
}

static uint32_t model_time(void *ctx)
{
    return ((struct model *)ctx)->time;
}

static void model_wait(void *ctx, unsigned int ms)
{
    ((struct model *)ctx)->time += ms;
}

static void model_barrier(void *ctx)
{
    (void)ctx;
}

static bool model_cancel(void *ctx)
{
    return ((struct model *)ctx)->cancel;
}

static const struct ls2k_usb_io model_io =
{
    model_read, model_write, model_time, model_wait, model_barrier, model_cancel
};

static void fresh(struct model *m)
{
    memset(m, 0, sizeof(*m));
    m->hc = (struct ls2k_ehci){&model_io, m, &storage, 0, false, false};
    m->regs[LUSB_PORT / 4] = 5;
    assert(ls2k_ehci_init(&m->hc, 0x02000000) == 0);
}

struct bot_model
{
    int step, fault;
    uint32_t tag, bytes;
};

static int bot_transfer(void *ctx, bool input, uint8_t *buffer, uint32_t length)
{
    struct bot_model *m = ctx;

    ++m->step;
    if (m->step == 1)
    {
        assert(!input && length == 31);
        assert(!memcmp(buffer, "USBC", 4));
        assert(buffer[13] == 0 && buffer[14] == 10);
        memcpy(&m->tag, buffer + 4, 4);
        memcpy(&m->bytes, buffer + 8, 4);
        if (m->fault == 1)
        {
            return 30;
        }
    }
    else if (m->step == 2 && m->bytes)
    {
        assert(input && length == m->bytes);
        memset(buffer, 0x5a, length);
        if (m->fault == 2)
        {
            return length - 1;
        }
    }
    else
    {
        uint32_t tag = m->tag + (m->fault == 4);

        assert(input && length == 13);
        memcpy(buffer, "USBS", 4);
        memcpy(buffer + 4, &tag, 4);
        buffer[8] = m->fault == 5;
        buffer[12] = m->fault == 6 ? 2 : (m->fault == 7 ? 1 : 0);
        if (m->fault == 3)
        {
            return 12;
        }
    }

    return length;
}

int main(void)
{
    struct model m;
    uint8_t setup[8] = {0x80, 6};
    uint8_t *allocation = malloc(65538);
    uint8_t *buffer = allocation + 1;
    struct ls2k_usb_transfer x = {0};

    assert(allocation && (uintptr_t)allocation > UINT32_MAX);
    fresh(&m);
    assert((uintptr_t)&storage > UINT32_MAX);
    x = (struct ls2k_usb_transfer){.address=2, .endpoint=1, .max_packet=512,
                                  .buffer=buffer, .length=65536, .timeout_ms=100};
    for (unsigned int pass = 0; pass < 2048; ++pass)
    {
        for (unsigned int direction = 0; direction < 2; ++direction)
        {
            m.bytes = 0;
            m.input = direction != 0;
            x.input = m.input;
            for (unsigned int i = 0; i < x.length; ++i)
            {
                buffer[i] = direction ? 0 : (uint8_t)(i * 37U + 11);
            }

            allocation[0] = allocation[65537] = 0xa5;
            assert(ls2k_ehci_transfer(&m.hc, &x) == 0 && x.actual == 65536);
            for (unsigned int i = 0; i < x.length; ++i)
            {
                assert(buffer[i] == (uint8_t)(i * 37U + 11));
            }
            assert(allocation[0] == 0xa5 && allocation[65537] == 0xa5);
        }
    }
    puts("PASS 256 MiB bidirectional DMA, high pointers, unaligned caller, guards");

    fresh(&m);
    m.control = m.input = true;
    m.short_packet = true;
    m.short_bytes = 18;
    x = (struct ls2k_usb_transfer){.address=2, .max_packet=64, .input=true,
                                  .setup=setup, .buffer=buffer, .length=64, .timeout_ms=100};
    memset(buffer, 0xcc, 64);
    assert(ls2k_ehci_transfer(&m.hc, &x) == 0 && x.actual == 18);
    assert(buffer[18] == 0xcc);
    m.short_bytes = 0;
    x.length = 0;
    assert(ls2k_ehci_transfer(&m.hc, &x) == 0 && x.actual == 0);
    puts("PASS control setup/data/status and short packet status stage");

    fresh(&m);
    m.periodic = m.input = true;
    x = (struct ls2k_usb_transfer){.address=3, .endpoint=1, .max_packet=64,
                                  .input=true, .interrupt=true, .buffer=buffer,
                                  .length=1, .timeout_ms=4, .interval_uframes=8};
    for (unsigned int interval = 1; interval <= 8192; interval <<= 1)
    {
        m.interval = x.interval_uframes = interval;
        m.regs[LUSB_FRAME / 4] = 1003 * 8;
        assert(ls2k_ehci_transfer(&m.hc, &x) == 0 && x.actual == 1);
    }
    puts("PASS all 14 high-speed interrupt intervals and periodic frame wrap");

    for (unsigned int fault = 0; fault < 8; ++fault)
    {
        fresh(&m);
        m.input = true;
        x.interrupt = false;
        x.timeout_ms = 10;
        m.timeout = fault == 0 || fault >= 5;
        m.disconnect = fault == 1;
        m.cancel = fault == 2;
        m.error = fault == 3 ? LUSB_HALT : (fault == 4 ? LUSB_HALT | 0x10 : 0);
        m.schedule_stuck = fault >= 6;
        m.halt_stuck = fault == 7;
        if (m.disconnect)
        {
            m.regs[LUSB_PORT / 4] = 0;
        }
        if (fault == 5)
        {
            m.time = UINT32_MAX - 4;
        }
        memset(buffer, 0xcc, 64);
        assert(ls2k_ehci_transfer(&m.hc, &x) < 0);
        assert(x.actual == 0 && buffer[0] == 0xcc);
        assert(m.time < 300);
        assert(m.hc.quarantined == (fault == 7));
        if (fault == 7)
        {
            struct ls2k_usb_dma saved = storage;

            assert(ls2k_ehci_transfer(&m.hc, &x) == -USB_ERR_NODEV);
            assert(ls2k_ehci_init(&m.hc, 0x02000000) < 0);
            assert(!memcmp(&saved, &storage, sizeof(saved)));
        }
        else
        {
            assert(ls2k_ehci_init(&m.hc, 0x02000000) == 0);
        }
    }
    fresh(&m);
    m.input = m.timeout = m.complete_on_retire = true;
    assert(ls2k_ehci_transfer(&m.hc, &x) == 0 && x.actual == 1);
    assert(x.toggle == ((storage.queue.token & LUSB_TOGGLE) != 0));
    puts("PASS timeout/completion race, disconnect, cancel, STALL, babble, wrap, quarantine");

    fresh(&m);
    assert(ls2k_ehci_init(&m.hc, 0x100000000ULL) < 0);
    assert(ls2k_ehci_init(&m.hc, 0xfffff000ULL) < 0);
    assert(ls2k_ehci_init(&m.hc, 0x200001) < 0);
    x.max_packet = 0;
    assert(ls2k_ehci_transfer(&m.hc, &x) == -USB_ERR_INVAL);
    fresh(&m);
    m.reset_stuck = true;
    assert(ls2k_ehci_init(&m.hc, 0x02000000) == -USB_ERR_TIMEOUT);
    assert(m.hc.quarantined);
    fresh(&m);
    m.input = true;
    m.regs[LUSB_STS / 4] |= LUSB_FATAL;
    x.max_packet = 64;
    assert(ls2k_ehci_transfer(&m.hc, &x) == -USB_ERR_IO);
    assert(!m.hc.ready);
    puts("PASS DMA range/alignment, invalid packets, reset deadline and fatal stop");

    for (unsigned int fault = 0; fault < 8; ++fault)
    {
        struct bot_model b = {.fault = fault};
        struct ls2k_usb_bot bot = {&b, bot_transfer, 0, false};
        uint8_t cdb[10] = {0x28};
        int ret = ls2k_usb_bot_command(&bot, cdb, 10, true, buffer, 512);

        assert((ret == 0) == (fault == 0));
        assert(bot.fault == (fault > 0 && fault < 7));
        if (bot.fault)
        {
            int step = b.step;

            assert(ls2k_usb_bot_command(&bot, cdb, 10, true, buffer, 512) < 0);
            assert(b.step == step);
        }
    }
    puts("PASS BOT short CBW/data/CSW, wrong tag/residue/status, CHECK CONDITION");
    free(allocation);
    puts("LS2K USB MODEL PASS");
    return 0;
}
