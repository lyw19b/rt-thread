/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __LS2K_GMAC_H__
#define __LS2K_GMAC_H__

#include <stddef.h>
#include <stdint.h>

/* LS2K1000 uses the four-word enhanced DWMAC descriptor format. */
#define GMAC_BUFFER_SIZE        1536U
#define GMAC_FRAME_MAX          1518U /* Ethernet header + VLAN + MTU, without FCS */
#define GMAC_DESC_OWN           (1U << 31)
#define GMAC_DESC_ERROR         (1U << 15)
#define GMAC_RX_FIRST           (1U << 9)
#define GMAC_RX_LAST            (1U << 8)
#define GMAC_RX_RING_END        (1U << 15)
#define GMAC_TX_RING_END        (1U << 21)
#define GMAC_TX_FIRST           (1U << 28)
#define GMAC_TX_LAST            (1U << 29)
#define GMAC_TX_INTERRUPT      (1U << 30)

#define GMAC_CONFIG             0x0000
#define GMAC_FILTER             0x0004
#define GMAC_MDIO_ADDR          0x0010
#define GMAC_MDIO_DATA          0x0014
#define GMAC_FLOW               0x0018
#define GMAC_VERSION            0x0020
#define GMAC_INT_MASK           0x003c
#define GMAC_ADDR_HIGH          0x0040
#define GMAC_ADDR_LOW           0x0044
#define GMAC_RGMII_STATUS       0x00d8
#define GMAC_DMA_BUS            0x1000
#define GMAC_DMA_TX_POLL        0x1004
#define GMAC_DMA_RX_POLL        0x1008
#define GMAC_DMA_RX_BASE        0x100c
#define GMAC_DMA_TX_BASE        0x1010
#define GMAC_DMA_STATUS         0x1014
#define GMAC_DMA_CONTROL        0x1018
#define GMAC_DMA_INT_ENABLE     0x101c
#define GMAC_DMA_TX_DONE        (1U << 0)
#define GMAC_DMA_RX_DONE        (1U << 6)
#define GMAC_DMA_RX_UNAVAILABLE (1U << 7)
#define GMAC_DMA_FATAL          (1U << 13)
#define GMAC_DMA_STATUS_W1C     0x1ffffU
#define GMAC_DMA_IRQ_MASK       ((1U << 16) | (1U << 15) | GMAC_DMA_FATAL | \
                                 GMAC_DMA_RX_UNAVAILABLE | GMAC_DMA_RX_DONE | GMAC_DMA_TX_DONE)

struct ls2k_gmac_desc
{
    volatile uint32_t status;
    uint32_t length;
    uint32_t buffer;
    uint32_t next;
};

/* DMA storage must be uncached, contiguous and addressable by 32-bit DMA. */
struct ls2k_gmac_ring
{
    struct ls2k_gmac_desc *desc;
    uint8_t *buffers;
    uint32_t desc_dma;
    uint32_t buffers_dma;
    unsigned int count;
};

/* All core calls are serialized by the RT-Thread adapter. The I/O boundary
 * also permits register/DMA fault injection without an emulated LS2K board. */
struct ls2k_gmac_io
{
    uint32_t (*read)(void *context, unsigned int reg);
    void (*write)(void *context, unsigned int reg, uint32_t value);
    void (*delay_ms)(void *context, unsigned int ms);
    void (*barrier)(void *context);
};

struct ls2k_gmac
{
    const struct ls2k_gmac_io *io;
    void *context;
    struct ls2k_gmac_ring tx, rx;
    unsigned int tx_head, tx_tail, tx_pending, rx_head;
    unsigned int phy, mdio_clock;
    unsigned int speed;
    int link, duplex, running;
    uint64_t tx_packets, rx_packets, tx_errors, rx_errors;
};

int ls2k_gmac_mdio_read(struct ls2k_gmac *mac, unsigned int phy,
                       unsigned int reg, uint16_t *value);
int ls2k_gmac_mdio_write(struct ls2k_gmac *mac, unsigned int phy,
                        unsigned int reg, uint16_t value);
int ls2k_gmac_start(struct ls2k_gmac *mac, const uint8_t address[6], int phy);
void ls2k_gmac_stop(struct ls2k_gmac *mac);
int ls2k_gmac_poll_link(struct ls2k_gmac *mac);
void ls2k_gmac_reclaim_tx(struct ls2k_gmac *mac);
uint8_t *ls2k_gmac_tx_buffer(struct ls2k_gmac *mac);
int ls2k_gmac_submit_tx(struct ls2k_gmac *mac, size_t length);
/* peek returns a frame length, 0 for an empty ring, or a negative error.
 * Every nonzero result must be followed by release, including dropped frames. */
int ls2k_gmac_peek_rx(struct ls2k_gmac *mac, const uint8_t **data);
void ls2k_gmac_release_rx(struct ls2k_gmac *mac);

#endif
