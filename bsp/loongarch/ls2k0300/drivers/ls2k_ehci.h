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
 *   Serialized EHCI control/bulk and high-speed interrupt transport.
 * Notes:
 *   The caller owns locking. All DMA uses a permanent uncached arena below
 *   4 GiB; arbitrary CPU buffers are copied and never truncated to bus addresses.
 */

#ifndef LS2K_EHCI_H
#define LS2K_EHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LUSB_BUFFER_SIZE 16384U
#define LUSB_CMD         0x00U
#define LUSB_STS         0x04U
#define LUSB_INTR        0x08U
#define LUSB_FRAME       0x0cU
#define LUSB_SEGMENT     0x10U
#define LUSB_PERIODIC    0x14U
#define LUSB_ASYNC       0x18U
#define LUSB_CONFIG      0x40U
#define LUSB_PORT        0x44U
#define LUSB_RUN         1U
#define LUSB_RESET       2U
#define LUSB_PSE         (1U << 4)
#define LUSB_ASE         (1U << 5)
#define LUSB_HALTED      (1U << 12)
#define LUSB_PSS         (1U << 14)
#define LUSB_ASS         (1U << 15)
#define LUSB_FATAL       (1U << 4)
#define LUSB_ACTIVE      (1U << 7)
#define LUSB_HALT        (1U << 6)
#define LUSB_IOC         (1U << 15)
#define LUSB_TOGGLE      (1U << 31)
#define LUSB_PORT_W1C    0x2aU

/* Extended descriptor words stay zero even on a 64-bit capable EHCI. */
struct ls2k_usb_qtd
{
    volatile uint32_t next, alternate, token, buffer[5];
    uint32_t upper[5], reserved[3];
} __attribute__((aligned(64)));

struct ls2k_usb_qh
{
    volatile uint32_t link, endpoint, capabilities, current;
    volatile uint32_t next, alternate, token, buffer[5];
    uint32_t upper[5], reserved[15];
} __attribute__((aligned(128)));

struct ls2k_usb_dma
{
    struct ls2k_usb_qh head, queue;
    struct ls2k_usb_qtd td[3];
    uint8_t setup[64];
    uint8_t buffer[LUSB_BUFFER_SIZE] __attribute__((aligned(4096)));
    uint32_t frames[1024] __attribute__((aligned(4096)));
} __attribute__((aligned(4096)));

struct ls2k_usb_io
{
    uint32_t (*read)(void *ctx, unsigned int offset);
    void (*write)(void *ctx, unsigned int offset, uint32_t value);
    uint32_t (*time_ms)(void *ctx);
    void (*wait_ms)(void *ctx, unsigned int ms);
    void (*barrier)(void *ctx);
    bool (*cancelled)(void *ctx);
};

struct ls2k_ehci
{
    const struct ls2k_usb_io *io;
    void *ctx;
    struct ls2k_usb_dma *dma;
    uint32_t dma_address;
    bool ready, quarantined;
};

struct ls2k_usb_transfer
{
    uint8_t address, endpoint, toggle;
    uint16_t max_packet;
    bool input, interrupt;
    const uint8_t *setup;
    uint8_t *buffer;
    uint32_t length, actual, timeout_ms, interval_uframes;
};

int ls2k_ehci_init(struct ls2k_ehci *hc, uint64_t dma_address);
int ls2k_ehci_stop(struct ls2k_ehci *hc);
int ls2k_ehci_transfer(struct ls2k_ehci *hc, struct ls2k_usb_transfer *transfer);

#endif
