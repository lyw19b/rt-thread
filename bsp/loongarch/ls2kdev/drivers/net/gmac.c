/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include "gmac.h"

_Static_assert(sizeof(struct ls2k_gmac_desc) == 16, "GMAC descriptor size");

#define MDIO_BUSY       1U
#define MDIO_WRITE      2U
#define MAC_RX          (1U << 2)
#define MAC_TX          (1U << 3)
#define MAC_DUPLEX      (1U << 11)
#define MAC_100         (1U << 14)
#define MAC_MII         (1U << 15)
#define DMA_RX_START    (1U << 1)
#define DMA_TX_START    (1U << 13)
#define PHY_BMCR        0
#define PHY_BMSR        1
#define PHY_LINK        (1U << 2)

static uint32_t _read(struct ls2k_gmac *mac, unsigned int reg)
{
    return mac->io->read(mac->context, reg);
}

static void _write(struct ls2k_gmac *mac, unsigned int reg, uint32_t value)
{
    mac->io->write(mac->context, reg, value);
}

static void _barrier(struct ls2k_gmac *mac)
{
    mac->io->barrier(mac->context);
}

static int _wait_clear(struct ls2k_gmac *mac, unsigned int reg, uint32_t mask)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 100; attempt++)
    {
        if (!(_read(mac, reg) & mask))
            return 0;
        mac->io->delay_ms(mac->context, 1);
    }
    return -ETIMEDOUT;
}

int ls2k_gmac_mdio_read(struct ls2k_gmac *mac, unsigned int phy,
                       unsigned int reg, uint16_t *value)
{
    int error;

    if (phy > 31 || reg > 31 || !value || mac->mdio_clock > 5)
        return -EINVAL;
    error = _wait_clear(mac, GMAC_MDIO_ADDR, MDIO_BUSY);
    if (error)
        return error;
    _write(mac, GMAC_MDIO_ADDR, (phy << 11) | (reg << 6) |
           (mac->mdio_clock << 2) | MDIO_BUSY);
    error = _wait_clear(mac, GMAC_MDIO_ADDR, MDIO_BUSY);
    if (!error)
        *value = _read(mac, GMAC_MDIO_DATA);
    return error;
}

int ls2k_gmac_mdio_write(struct ls2k_gmac *mac, unsigned int phy,
                        unsigned int reg, uint16_t value)
{
    int error;

    if (phy > 31 || reg > 31 || mac->mdio_clock > 5)
        return -EINVAL;
    error = _wait_clear(mac, GMAC_MDIO_ADDR, MDIO_BUSY);
    if (error)
        return error;
    _write(mac, GMAC_MDIO_DATA, value);
    _write(mac, GMAC_MDIO_ADDR, (phy << 11) | (reg << 6) |
           (mac->mdio_clock << 2) | MDIO_BUSY | MDIO_WRITE);
    return _wait_clear(mac, GMAC_MDIO_ADDR, MDIO_BUSY);
}

static int _phy_init(struct ls2k_gmac *mac, int address)
{
    unsigned int first = address < 0 ? 0 : (unsigned int)address;
    unsigned int last = address < 0 ? 31 : (unsigned int)address;
    unsigned int phy;
    uint16_t id1, id2, value;
    int error;

    for (phy = first; phy <= last; phy++)
    {
        error = ls2k_gmac_mdio_read(mac, phy, 2, &id1);
        if (error)
            return error;
        error = ls2k_gmac_mdio_read(mac, phy, 3, &id2);
        if (error)
            return error;
        if (((uint32_t)id1 << 16 | id2) != 0 &&
            ((uint32_t)id1 << 16 | id2) != UINT32_MAX)
            break;
    }
    if (phy > last)
        return -ENODEV;
    mac->phy = phy;
    /* Keep the bootloader's board-specific RGMII delay settings. Advertise
     * standard 10/100 and 1000 modes, then restart Clause 22 autonegotiation. */
    error = ls2k_gmac_mdio_read(mac, phy, 4, &value);
    if (!error)
        error = ls2k_gmac_mdio_write(mac, phy, 4, value | 0x01e1);
    if (!error)
        error = ls2k_gmac_mdio_read(mac, phy, 9, &value);
    if (!error)
        error = ls2k_gmac_mdio_write(mac, phy, 9, value | 0x0300);
    if (!error)
        error = ls2k_gmac_mdio_read(mac, phy, PHY_BMCR, &value);
    if (!error)
        error = ls2k_gmac_mdio_write(mac, phy, PHY_BMCR, (value & ~0x0c00) | 0x1200);
    return error;
}

static int _ring_valid(const struct ls2k_gmac_ring *ring)
{
    return ring->desc && ring->buffers && ring->count >= 2 && ring->count <= 256 &&
           !(ring->desc_dma & 15) && !(ring->buffers_dma & 15) &&
           (uint64_t)ring->desc_dma + ring->count * sizeof(*ring->desc) <= (1ULL << 32) &&
           (uint64_t)ring->buffers_dma + ring->count * GMAC_BUFFER_SIZE <= (1ULL << 32);
}

static void _rings_init(struct ls2k_gmac *mac)
{
    unsigned int i;

    mac->tx_head = mac->tx_tail = mac->tx_pending = mac->rx_head = 0;
    for (i = 0; i < mac->tx.count; i++)
    {
        struct ls2k_gmac_desc *desc = &mac->tx.desc[i];
        desc->status = i + 1 == mac->tx.count ? GMAC_TX_RING_END : 0;
        desc->length = 0;
        desc->buffer = mac->tx.buffers_dma + i * GMAC_BUFFER_SIZE;
        desc->next = 0;
    }
    for (i = 0; i < mac->rx.count; i++)
    {
        struct ls2k_gmac_desc *desc = &mac->rx.desc[i];
        desc->length = GMAC_BUFFER_SIZE | (i + 1 == mac->rx.count ? GMAC_RX_RING_END : 0);
        desc->buffer = mac->rx.buffers_dma + i * GMAC_BUFFER_SIZE;
        desc->next = 0;
        desc->status = GMAC_DESC_OWN;
    }
    _barrier(mac);
}

void ls2k_gmac_stop(struct ls2k_gmac *mac)
{
    _write(mac, GMAC_DMA_INT_ENABLE, 0);
    _write(mac, GMAC_DMA_CONTROL, _read(mac, GMAC_DMA_CONTROL) & ~(DMA_TX_START | DMA_RX_START));
    _write(mac, GMAC_CONFIG, _read(mac, GMAC_CONFIG) & ~(MAC_TX | MAC_RX));
    _barrier(mac);
    mac->running = mac->link = 0;
}

int ls2k_gmac_start(struct ls2k_gmac *mac, const uint8_t address[6], int phy)
{
    int error;

    if (!_ring_valid(&mac->tx) || !_ring_valid(&mac->rx) || phy < -1 || phy > 31)
        return -EINVAL;
    /* A reset must finish before the CPU can rewrite DMA-owned descriptors. */
    ls2k_gmac_stop(mac);
    _write(mac, GMAC_DMA_BUS, 1);
    error = _wait_clear(mac, GMAC_DMA_BUS, 1);
    if (error)
        return error;
    error = _phy_init(mac, phy);
    if (error)
        return error;
    _rings_init(mac);
    _write(mac, GMAC_ADDR_HIGH, (uint32_t)address[5] << 8 | address[4]);
    _write(mac, GMAC_ADDR_LOW, (uint32_t)address[3] << 24 | (uint32_t)address[2] << 16 |
           (uint32_t)address[1] << 8 | address[0]);
    _write(mac, GMAC_DMA_TX_BASE, mac->tx.desc_dma);
    _write(mac, GMAC_DMA_RX_BASE, mac->rx.desc_dma);
    /* Four-word descriptors, 32-beat bursts. No checksum offload. */
    _write(mac, GMAC_DMA_BUS, (1U << 24) | (32U << 8));
    _write(mac, GMAC_CONFIG, MAC_DUPLEX | MAC_TX | MAC_RX);
    /* Pass multicast for lwIP IGMP/IPv6; keep unicast address filtering. */
    _write(mac, GMAC_FILTER, 1U << 4);
    _write(mac, GMAC_FLOW, 0);
    _write(mac, GMAC_INT_MASK, 0x020f); /* PHY is polled outside interrupt context. */
    _write(mac, 0x010c, UINT32_MAX); /* MMC RX mask */
    _write(mac, 0x0110, UINT32_MAX); /* MMC TX mask */
    _write(mac, 0x0200, UINT32_MAX); /* MMC checksum mask */
    _write(mac, GMAC_DMA_STATUS, GMAC_DMA_STATUS_W1C);
    _write(mac, GMAC_DMA_CONTROL, (1U << 25) | (1U << 21) | (1U << 2) |
           DMA_RX_START | DMA_TX_START);
    /* The adapter installs its ISR before enabling these interrupts. */
    mac->running = 1;
    return 0;
}

int ls2k_gmac_poll_link(struct ls2k_gmac *mac)
{
    uint16_t status;
    uint32_t rgmii, config;
    int error;

    /* BMSR link status is latched low: read it twice after a cable change. */
    error = ls2k_gmac_mdio_read(mac, mac->phy, PHY_BMSR, &status);
    if (!error)
        error = ls2k_gmac_mdio_read(mac, mac->phy, PHY_BMSR, &status);
    if (error || !(status & PHY_LINK))
    {
        mac->link = 0;
        return error;
    }
    rgmii = _read(mac, GMAC_RGMII_STATUS);
    mac->speed = (rgmii & 6) == 4 ? 1000 : (rgmii & 6) == 2 ? 100 : 10;
    mac->duplex = !!(rgmii & 1);
    config = _read(mac, GMAC_CONFIG) & ~(MAC_MII | MAC_100 | MAC_DUPLEX);
    if (mac->speed != 1000)
        config |= MAC_MII;
    if (mac->speed == 100)
        config |= MAC_100;
    if (mac->duplex)
        config |= MAC_DUPLEX;
    _write(mac, GMAC_CONFIG, config);
    mac->link = 1;
    return 0;
}

void ls2k_gmac_reclaim_tx(struct ls2k_gmac *mac)
{
    while (mac->tx_pending)
    {
        struct ls2k_gmac_desc *desc = &mac->tx.desc[mac->tx_tail];
        uint32_t status = desc->status;
        if (status & GMAC_DESC_OWN)
            break;
        _barrier(mac);
        if (status & GMAC_DESC_ERROR)
            mac->tx_errors++;
        else
            mac->tx_packets++;
        desc->length = 0;
        desc->status = mac->tx_tail + 1 == mac->tx.count ? GMAC_TX_RING_END : 0;
        mac->tx_tail = (mac->tx_tail + 1) % mac->tx.count;
        mac->tx_pending--;
    }
}

uint8_t *ls2k_gmac_tx_buffer(struct ls2k_gmac *mac)
{
    ls2k_gmac_reclaim_tx(mac);
    if (!mac->running || mac->tx_pending == mac->tx.count)
        return NULL;
    return mac->tx.buffers + mac->tx_head * GMAC_BUFFER_SIZE;
}

int ls2k_gmac_submit_tx(struct ls2k_gmac *mac, size_t length)
{
    struct ls2k_gmac_desc *desc;
    uint32_t status;

    if (!length || length > GMAC_FRAME_MAX)
        return -EMSGSIZE;
    if (!mac->running || mac->tx_pending == mac->tx.count)
        return -ENOBUFS;
    desc = &mac->tx.desc[mac->tx_head];
    if (desc->status & GMAC_DESC_OWN)
        return -EBUSY;
    status = GMAC_TX_FIRST | GMAC_TX_LAST | GMAC_TX_INTERRUPT;
    if (mac->tx_head + 1 == mac->tx.count)
        status |= GMAC_TX_RING_END;
    desc->length = length;
    /* Publish payload and fields before ownership. DMA may fetch immediately. */
    _barrier(mac);
    desc->status = status | GMAC_DESC_OWN;
    _barrier(mac);
    mac->tx_head = (mac->tx_head + 1) % mac->tx.count;
    mac->tx_pending++;
    _write(mac, GMAC_DMA_TX_POLL, 0);
    return 0;
}

int ls2k_gmac_peek_rx(struct ls2k_gmac *mac, const uint8_t **data)
{
    uint32_t status = mac->rx.desc[mac->rx_head].status;
    unsigned int length;

    if (!mac->running || (status & GMAC_DESC_OWN))
        return 0;
    _barrier(mac);
    length = (status >> 16) & 0x3fff;
    if ((status & GMAC_DESC_ERROR) || !(status & GMAC_RX_FIRST) ||
        !(status & GMAC_RX_LAST) || length < 18 || length > GMAC_FRAME_MAX + 4)
    {
        mac->rx_errors++;
        return -EIO;
    }
    /* The MAC retains FCS; lwIP expects the Ethernet frame without it. */
    *data = mac->rx.buffers + mac->rx_head * GMAC_BUFFER_SIZE;
    return length - 4;
}

void ls2k_gmac_release_rx(struct ls2k_gmac *mac)
{
    _barrier(mac);
    mac->rx.desc[mac->rx_head].status = GMAC_DESC_OWN;
    _barrier(mac);
    mac->rx_head = (mac->rx_head + 1) % mac->rx.count;
    _write(mac, GMAC_DMA_RX_POLL, 0);
}
