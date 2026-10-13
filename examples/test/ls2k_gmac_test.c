/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side DMA/MDIO model for the production GMAC core. This does not model
 * LS2K bus timing, PHY electrical behavior, or the RT-Thread interrupt path.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "gmac.h"

#define RING_COUNT 8
struct model
{
    struct ls2k_gmac mac;
    uint32_t registers[0x1100 / 4];
    uint16_t phy[32][32];
    struct ls2k_gmac_desc tx[RING_COUNT], rx[RING_COUNT];
    uint8_t tx_data[RING_COUNT][GMAC_BUFFER_SIZE];
    uint8_t rx_data[RING_COUNT][GMAC_BUFFER_SIZE];
    unsigned int delays, barriers, tx_polls;
    int reset_stuck, mdio_stuck;
    int publishing;
    unsigned int publication_barriers;
};

static uint32_t model_read(void *context, unsigned int reg)
{
    struct model *m = context;
    assert(reg < sizeof(m->registers));
    return m->registers[reg / 4];
}

static void model_write(void *context, unsigned int reg, uint32_t value)
{
    struct model *m = context;
    assert(reg < sizeof(m->registers));
    if (reg == GMAC_DMA_STATUS)
        m->registers[reg / 4] &= ~value;
    else
        m->registers[reg / 4] = value;
    if (reg == GMAC_DMA_BUS && (value & 1) && !m->reset_stuck)
        m->registers[reg / 4] &= ~1U;
    if (reg == GMAC_MDIO_ADDR && (value & 1) && !m->mdio_stuck)
    {
        unsigned int phy = (value >> 11) & 31;
        unsigned int index = (value >> 6) & 31;
        if (value & 2)
            m->phy[phy][index] = m->registers[GMAC_MDIO_DATA / 4];
        else
            m->registers[GMAC_MDIO_DATA / 4] = m->phy[phy][index];
        m->registers[reg / 4] &= ~1U;
    }
    if (reg == GMAC_DMA_TX_POLL)
        m->tx_polls++;
}

static void model_delay(void *context, unsigned int ms)
{
    struct model *m = context;
    m->delays += ms;
}

static void model_barrier(void *context)
{
    struct model *m = context;
    m->barriers++;
    if (m->publishing)
    {
        struct ls2k_gmac_desc *desc = &m->tx[m->mac.tx_head];
        /* First barrier publishes data, second barrier publishes ownership. */
        assert(!!(desc->status & GMAC_DESC_OWN) == !!m->publication_barriers);
        assert(desc->length == 64);
        assert(m->tx_data[m->mac.tx_head][0] == 0xab);
        m->publication_barriers++;
    }
}

static const struct ls2k_gmac_io io =
{
    model_read, model_write, model_delay, model_barrier
};
static const uint8_t address[6] = {2, 0, 0, 0, 1, 1};

static void setup(struct model *m)
{
    memset(m, 0, sizeof(*m));
    memset(m->phy, 0xff, sizeof(m->phy));
    memset(m->phy[3], 0, sizeof(m->phy[3]));
    m->phy[3][2] = 0x001c;
    m->phy[3][3] = 0xc915;
    m->mac.io = &io;
    m->mac.context = m;
    m->mac.mdio_clock = 1;
    m->mac.tx = (struct ls2k_gmac_ring){m->tx, &m->tx_data[0][0], 0x1000, 0x10000, RING_COUNT};
    m->mac.rx = (struct ls2k_gmac_ring){m->rx, &m->rx_data[0][0], 0x2000, 0x20000, RING_COUNT};
}

static void test_start_and_errors(void)
{
    struct model m;
    uint16_t value;

    setup(&m);
    assert(ls2k_gmac_start(&m.mac, address, -1) == 0);
    assert(m.mac.phy == 3 && m.mac.running && !m.mac.link);
    assert(m.registers[GMAC_DMA_TX_BASE / 4] == 0x1000);
    assert(m.registers[GMAC_DMA_RX_BASE / 4] == 0x2000);
    assert(m.registers[GMAC_ADDR_LOW / 4] == 2);
    assert(m.registers[GMAC_ADDR_HIGH / 4] == 0x101);
    assert(m.phy[3][0] & 0x1200);
    assert(m.rx[RING_COUNT - 1].length & GMAC_RX_RING_END);
    assert(m.tx[RING_COUNT - 1].status & GMAC_TX_RING_END);
    assert(ls2k_gmac_mdio_read(&m.mac, 32, 0, &value) == -EINVAL);
    assert(ls2k_gmac_mdio_write(&m.mac, 3, 32, 0) == -EINVAL);
    ls2k_gmac_stop(&m.mac);
    assert(!m.mac.running && !m.mac.link);
    assert(m.registers[GMAC_DMA_INT_ENABLE / 4] == 0);

    setup(&m);
    m.reset_stuck = 1;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -ETIMEDOUT);
    assert(m.delays == 100 && !m.mac.running);
    setup(&m);
    m.mdio_stuck = 1;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -ETIMEDOUT);
    assert(m.delays == 100 && !m.mac.running);
    setup(&m);
    memset(m.phy, 0xff, sizeof(m.phy));
    assert(ls2k_gmac_start(&m.mac, address, -1) == -ENODEV);
    setup(&m);
    m.mac.tx.buffers_dma = 0xfffffff0;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -EINVAL);
    m.mac.tx.buffers_dma = 3;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -EINVAL);
    setup(&m);
    m.mac.rx.count = 0;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -EINVAL);
    m.mac.rx.count = 2;
    assert(ls2k_gmac_start(&m.mac, address, 3) == -EINVAL);
    m.mac.rx.count = m.mac.tx.count = 3;
    assert(ls2k_gmac_start(&m.mac, address, 3) == 0);
    assert(m.rx[2].length & GMAC_RX_RING_END);
    assert(m.tx[2].status & GMAC_TX_RING_END);
    puts("GMAC initialization, address bounds and timeout tests: PASS");
}

static void test_link(void)
{
    struct model m;
    unsigned int i;
    const unsigned int speeds[] = {10, 100, 1000};

    setup(&m);
    assert(ls2k_gmac_start(&m.mac, address, -1) == 0);
    for (i = 0; i < 3; i++)
    {
        m.phy[3][1] = 4;
        m.registers[GMAC_RGMII_STATUS / 4] = (i << 1) | 1 | 8;
        assert(ls2k_gmac_poll_link(&m.mac) == 0);
        assert(m.mac.link && m.mac.speed == speeds[i] && m.mac.duplex);
        assert(!!(m.registers[GMAC_CONFIG / 4] & (1U << 15)) == (i != 2));
        assert(!!(m.registers[GMAC_CONFIG / 4] & (1U << 14)) == (i == 1));
        m.phy[3][1] = 0;
        assert(ls2k_gmac_poll_link(&m.mac) == 0 && !m.mac.link);
    }
    m.phy[3][1] = 4;
    m.registers[GMAC_RGMII_STATUS / 4] = 8;
    assert(ls2k_gmac_poll_link(&m.mac) == 0 && !m.mac.duplex);
    m.mdio_stuck = 1;
    assert(ls2k_gmac_poll_link(&m.mac) == -ETIMEDOUT && !m.mac.link);
    puts("GMAC link transitions and speed/duplex tests: PASS");
}

static void test_tx(void)
{
    struct model m;
    unsigned int i;

    setup(&m);
    assert(ls2k_gmac_start(&m.mac, address, 3) == 0);
    assert(ls2k_gmac_submit_tx(&m.mac, 0) == -EMSGSIZE);
    assert(ls2k_gmac_submit_tx(&m.mac, GMAC_FRAME_MAX + 1) == -EMSGSIZE);
    for (i = 0; i < RING_COUNT; i++)
    {
        uint8_t *buffer = ls2k_gmac_tx_buffer(&m.mac);
        assert(buffer == m.tx_data[i]);
        memset(buffer, 0xab, 64);
        m.publishing = 1;
        m.publication_barriers = 0;
        assert(ls2k_gmac_submit_tx(&m.mac, 64) == 0);
        m.publishing = 0;
        assert(m.publication_barriers == 2);
        assert(m.tx[i].status & GMAC_DESC_OWN);
    }
    assert(ls2k_gmac_tx_buffer(&m.mac) == NULL);
    assert(ls2k_gmac_submit_tx(&m.mac, 64) == -ENOBUFS);
    assert(m.mac.tx_pending == RING_COUNT && m.mac.tx_head == 0);
    for (i = 0; i < RING_COUNT; i++)
        m.tx[i].status &= ~GMAC_DESC_OWN;
    m.tx[2].status |= GMAC_DESC_ERROR;
    ls2k_gmac_reclaim_tx(&m.mac);
    assert(m.mac.tx_packets == RING_COUNT - 1 && m.mac.tx_errors == 1);
    assert(!m.mac.tx_pending && m.mac.tx_tail == 0);
    assert(m.tx[RING_COUNT - 1].status == GMAC_TX_RING_END);
    puts("GMAC TX ownership, queue exhaustion and reclaim tests: PASS");
}

static void test_rx(void)
{
    struct model m;
    const uint8_t *data;
    const unsigned int lengths[] = {0, 17, 18, 64, GMAC_FRAME_MAX + 4, 1536, 0x3fff};
    unsigned int i;

    setup(&m);
    assert(ls2k_gmac_start(&m.mac, address, 3) == 0);
    assert(ls2k_gmac_peek_rx(&m.mac, &data) == 0);
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
        unsigned int index = m.mac.rx_head;
        int length;
        m.rx[index].status = (lengths[i] << 16) | GMAC_RX_FIRST | GMAC_RX_LAST;
        length = ls2k_gmac_peek_rx(&m.mac, &data);
        if (lengths[i] >= 18 && lengths[i] <= GMAC_FRAME_MAX + 4)
        {
            assert(length == (int)lengths[i] - 4);
            assert(data == m.rx_data[index]);
        }
        else
            assert(length == -EIO);
        ls2k_gmac_release_rx(&m.mac);
        assert(m.rx[index].status == GMAC_DESC_OWN);
    }
    m.rx[m.mac.rx_head].status = (64U << 16) | GMAC_RX_FIRST;
    assert(ls2k_gmac_peek_rx(&m.mac, &data) == -EIO);
    ls2k_gmac_release_rx(&m.mac);
    assert(m.mac.rx_head == 0);
    m.rx[0].status = (64U << 16) | GMAC_RX_FIRST | GMAC_RX_LAST | GMAC_DESC_ERROR;
    assert(ls2k_gmac_peek_rx(&m.mac, &data) == -EIO);
    ls2k_gmac_release_rx(&m.mac);
    assert(m.rx[RING_COUNT - 1].length & GMAC_RX_RING_END);
    puts("GMAC RX malformed frame, FCS and ring wrap tests: PASS");
}

static void test_stress(void)
{
    static struct model ports[2];
    unsigned int iteration, port;

    for (port = 0; port < 2; port++)
    {
        setup(&ports[port]);
        assert(ls2k_gmac_start(&ports[port].mac, address, 3) == 0);
    }
    for (iteration = 0; iteration < 100000; iteration++)
    {
        for (port = 0; port < 2; port++)
        {
            struct model *m = &ports[port];
            unsigned int tx = m->mac.tx_head, rx = m->mac.rx_head;
            size_t length = 14 + iteration % (GMAC_FRAME_MAX - 13);
            uint8_t *buffer = ls2k_gmac_tx_buffer(&m->mac);
            const uint8_t *received;
            assert(buffer);
            memset(buffer, (iteration + port) & 255, length);
            assert(ls2k_gmac_submit_tx(&m->mac, length) == 0);
            /* DMA transfers the payload and appends an FCS before completion. */
            assert(m->tx[tx].buffer == 0x10000 + tx * GMAC_BUFFER_SIZE);
            memcpy(m->rx_data[rx], buffer, length);
            m->tx[tx].status &= ~GMAC_DESC_OWN;
            m->rx[rx].status = ((length + 4) << 16) | GMAC_RX_FIRST | GMAC_RX_LAST;
            assert(ls2k_gmac_peek_rx(&m->mac, &received) == (int)length);
            assert(memcmp(received, buffer, length) == 0);
            ls2k_gmac_release_rx(&m->mac);
            ls2k_gmac_reclaim_tx(&m->mac);
            assert(!m->mac.tx_pending);
        }
    }
    for (port = 0; port < 2; port++)
    {
        assert(ports[port].mac.tx_packets == 100000);
        /* Recovery resets indices only after DMA reset; counters survive. */
        assert(ls2k_gmac_start(&ports[port].mac, address, 3) == 0);
        assert(!ports[port].mac.tx_head && !ports[port].mac.rx_head);
        assert(ports[port].mac.tx_packets == 100000);
    }
    puts("GMAC two-port DMA stress: 100000 frames/port PASS");
}

int main(void)
{
    test_start_and_errors();
    test_link();
    test_tx();
    test_rx();
    test_stress();
    return 0;
}
