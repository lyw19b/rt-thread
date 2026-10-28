/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add bounded native xHCI mouse transport
 */

/*
 * Description:
 *   Provide a serialized, polling xHCI transport for root-port USB2 mice.
 */

#ifndef LS2K3000_XHCI_MOUSE_H
#define LS2K3000_XHCI_MOUSE_H

#include "hid_mouse.h"
#include "periph_io.h"

#define LS_XHCI_RING_TRBS 64U
#define LS_XHCI_EVENTS 256U
#define LS_XHCI_SCRATCHPADS 32U
#define LS_XHCI_BUFFER_BYTES 4096U

struct ls_xhci_trb
{
    uint32_t parameter_low;
    uint32_t parameter_high;
    uint32_t status;
    uint32_t control;
};

/* Permanently reserved coherent memory. Never free on a failed hardware stop. */
struct ls_xhci_dma
{
    uint64_t dcbaa[256] __attribute__((aligned(4096)));
    struct ls_xhci_trb command[LS_XHCI_RING_TRBS] __attribute__((aligned(4096)));
    struct ls_xhci_trb event[LS_XHCI_EVENTS] __attribute__((aligned(4096)));
    struct ls_xhci_trb ep0[LS_XHCI_RING_TRBS] __attribute__((aligned(4096)));
    struct ls_xhci_trb interrupt[LS_XHCI_RING_TRBS] __attribute__((aligned(4096)));
    uint32_t erst[4] __attribute__((aligned(64)));
    uint8_t output_context[32 * 64] __attribute__((aligned(64)));
    uint8_t input_context[33 * 64] __attribute__((aligned(64)));
    uint8_t transfer[LS_XHCI_BUFFER_BYTES] __attribute__((aligned(4096)));
    uint8_t report[128] __attribute__((aligned(64)));
    uint64_t scratch_pointer[LS_XHCI_SCRATCHPADS] __attribute__((aligned(64)));
    uint8_t scratch[LS_XHCI_SCRATCHPADS][4096] __attribute__((aligned(4096)));
};

struct ls_xhci_ops
{
    uint32_t (*read32)(void *context, uint32_t offset);
    void (*write32)(void *context, uint32_t offset, uint32_t value);
    void (*barrier)(void *context);
    uint64_t (*now_ms)(void *context);
    void (*sleep_ms)(void *context, unsigned int milliseconds);
    void (*motion)(void *context, const struct ls_mouse_motion *motion, int connected);
};

struct ls_xhci_ring
{
    unsigned int enqueue;
    unsigned int cycle;
};

struct ls_xhci
{
    const struct ls_xhci_ops *ops;
    void *context;
    struct ls_xhci_dma *dma;
    uint64_t dma_address;
    uint64_t bar_bytes;
    uint32_t operational;
    uint32_t runtime;
    uint32_t doorbell;
    unsigned int ports;
    unsigned int context_bytes;
    unsigned int event_dequeue;
    unsigned int event_cycle;
    struct ls_xhci_ring command;
    struct ls_xhci_ring ep0;
    struct ls_xhci_ring interrupt;
    uint8_t inspected[256];
    uint8_t usb2_port[256];
    uint8_t slot;
    uint8_t port;
    uint8_t speed;
    uint8_t endpoint;
    uint8_t interface_number;
    uint8_t configuration;
    uint8_t interval;
    uint16_t packet_bytes;
    uint16_t vendor;
    uint16_t product;
    struct ls_mouse_layout layout;
    uint64_t pending_command;
    uint64_t control_trbs[3];
    uint64_t pending_interrupt;
    unsigned int command_complete;
    unsigned int command_code;
    unsigned int command_slot;
    unsigned int control_complete;
    unsigned int control_code;
    unsigned int control_bytes;
    unsigned int control_length;
    unsigned int interrupt_ready;
    unsigned int interrupt_bytes;
    unsigned int running;
    unsigned int quarantined;
    unsigned int connected;
    uint64_t commands;
    uint64_t reports;
    uint64_t errors;
    uint64_t timeouts;
    uint64_t malformed;
};

/* One owner thread must serialize all calls and all ring access. Callback does
 * not reenter this API. Polling sleeps yield the CPU; no kernel spinlock is held. */
int ls_xhci_start(struct ls_xhci *host);
int ls_xhci_stop(struct ls_xhci *host);
int ls_xhci_poll(struct ls_xhci *host);

#endif
