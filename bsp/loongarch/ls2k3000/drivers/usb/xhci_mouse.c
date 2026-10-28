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
 *   Own xHCI rings explicitly; publish TRBs last and bound every hardware wait.
 */

#include "xhci_mouse.h"
#include <string.h>

#define TRB_TYPE(value) ((uint32_t)(value) << 10)
#define TRB_IOC (1U << 5)
#define TRB_ISP (1U << 2)
#define TRB_IDT (1U << 6)
#define TRB_DIRECTION (1U << 16)
#define COMPLETION_SUCCESS 1U
#define COMPLETION_SHORT 13U
#define PORT_CONNECTED 1U
#define PORT_ENABLED 2U
#define PORT_RESET (1U << 4)
#define PORT_POWER (1U << 9)
#define PORT_CHANGES (0x7fU << 17)
#define WAIT_MS 1000U

_Static_assert(sizeof(struct ls_xhci_trb) == 16, "xHCI TRB must be 16 bytes");
_Static_assert(offsetof(struct ls_xhci_dma, event) % 4096 == 0, "event ring alignment");
_Static_assert(offsetof(struct ls_xhci_dma, transfer) % 4096 == 0, "control buffer alignment");
_Static_assert(sizeof(struct ls_xhci_dma) % 4096 == 0, "DMA arena page extent");

static uint32_t read32(struct ls_xhci *host, uint32_t offset)
{
    return host->ops->read32(host->context, offset);
}

static void write32(struct ls_xhci *host, uint32_t offset, uint32_t value)
{
    host->ops->write32(host->context, offset, value);
}

static void barrier(struct ls_xhci *host)
{
    host->ops->barrier(host->context);
}

static uint64_t address(struct ls_xhci *host, const void *pointer)
{
    return host->dma_address + (const uint8_t *)pointer - (const uint8_t *)host->dma;
}

static void write64(struct ls_xhci *host, uint32_t offset, uint64_t value)
{
    /* xHCI permits a low DWORD followed by the high DWORD for 64-bit registers. */
    write32(host, offset, (uint32_t)value);
    write32(host, offset + 4, (uint32_t)(value >> 32));
}

static int wait_register(struct ls_xhci *host, uint32_t offset,
                         uint32_t mask, uint32_t expected)
{
    uint64_t started = host->ops->now_ms(host->context);

    do
    {
        uint32_t value = read32(host, offset);

        if (value == UINT32_MAX)
        {
            return LS_IO_ERROR;
        }

        if ((value & mask) == expected)
        {
            return LS_OK;
        }

        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < WAIT_MS);

    host->timeouts++;
    return LS_TIMEOUT;
}

static void release_mouse(struct ls_xhci *host)
{
    if (host->connected)
    {
        struct ls_mouse_motion released = {0};

        released.button_mask = 0xff;
        host->connected = 0;
        host->ops->motion(host->context, &released, 0);
    }
}

int ls_xhci_stop(struct ls_xhci *host)
{
    int result;
    uint32_t command;

    if (!host || !host->ops || !host->operational)
    {
        return LS_INVALID;
    }

    host->running = 0;
    release_mouse(host);
    command = read32(host, host->operational);

    if (command == UINT32_MAX)
    {
        host->quarantined = 1;
        return LS_IO_ERROR;
    }

    write32(host, host->operational, command & ~5U);
    result = wait_register(host, host->operational + 4, 1, 1);

    if (result)
    {
        host->quarantined = 1;
    }

    return result;
}

static int fail_controller(struct ls_xhci *host, int result)
{
    host->errors++;
    ls_xhci_stop(host);
    return result;
}

static int extended_capabilities(struct ls_xhci *host, uint32_t capabilities)
{
    uint32_t offset = (capabilities >> 16) * 4;
    unsigned int visited = 0;
    unsigned int protocols = 0;

    while (offset)
    {
        uint32_t header;
        uint32_t next;

        if (offset < 0x20 || offset > host->bar_bytes - 16 || ++visited > 128)
        {
            return LS_INVALID;
        }

        header = read32(host, offset);
        next = ((header >> 8) & 0xff) * 4;

        if ((header & 0xff) == 1)
        {
            uint32_t control;
            int result;

            write32(host, offset, header | (1U << 24));
            result = wait_register(host, offset, 1U << 16, 0);

            if (result)
            {
                return result;
            }

            /* USBLEGCTLSTS: disable SMI enables, acknowledge defined W1C events. */
            control = read32(host, offset + 4);
            control &= (7U << 1) | (0xffU << 5) | (7U << 17);
            write32(host, offset + 4, control | (7U << 29));
        }
        else if ((header & 0xff) == 2)
        {
            uint32_t ports = read32(host, offset + 8);
            unsigned int first = ports & 0xff;
            unsigned int count = (ports >> 8) & 0xff;

            if (count && (!first || first > host->ports || count > host->ports - first + 1))
            {
                return LS_INVALID;
            }

            protocols++;

            for (unsigned int index = first; index < first + count; ++index)
            {
                host->usb2_port[index] = (header >> 24) == 2;
            }
        }

        if (next && (offset + next <= offset || offset + next > host->bar_bytes - 16))
        {
            return LS_INVALID;
        }

        offset = next ? offset + next : 0;
    }

    /* Do not guess a USB3 port's speed IDs when firmware omits protocol data. */
    return protocols ? LS_OK : LS_UNSUPPORTED;
}

static void ring_initialize(struct ls_xhci *host, struct ls_xhci_trb *trbs,
                             struct ls_xhci_ring *ring)
{
    memset(trbs, 0, LS_XHCI_RING_TRBS * sizeof(*trbs));
    ring->enqueue = 0;
    ring->cycle = 1;
    trbs[LS_XHCI_RING_TRBS - 1].parameter_low = (uint32_t)address(host, trbs);
    trbs[LS_XHCI_RING_TRBS - 1].parameter_high = address(host, trbs) >> 32;
    trbs[LS_XHCI_RING_TRBS - 1].control = TRB_TYPE(6) | 2;
}

static uint64_t ring_submit(struct ls_xhci *host, struct ls_xhci_trb *trbs,
                            struct ls_xhci_ring *ring, uint64_t parameter,
                            uint32_t status, uint32_t control)
{
    struct ls_xhci_trb *trb = &trbs[ring->enqueue];
    uint64_t physical = address(host, trb);

    trb->parameter_low = parameter;
    trb->parameter_high = parameter >> 32;
    trb->status = status;
    barrier(host);
    *(volatile uint32_t *)&trb->control = control | ring->cycle;
    ring->enqueue++;

    if (ring->enqueue == LS_XHCI_RING_TRBS - 1)
    {
        barrier(host);
        *(volatile uint32_t *)&trbs[ring->enqueue].control = TRB_TYPE(6) | 2 | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }

    barrier(host);
    return physical;
}

static void transfer_event(struct ls_xhci *host, const struct ls_xhci_trb *event)
{
    uint64_t pointer = (uint64_t)event->parameter_low | (uint64_t)event->parameter_high << 32;
    unsigned int endpoint = (event->control >> 16) & 0x1f;
    unsigned int code = event->status >> 24;
    unsigned int residual = event->status & 0xffffff;

    if ((event->control >> 24) != host->slot || !host->slot)
    {
        return;
    }

    if (event->control & (1U << 2))
    {
        /* We never submit Event Data TRBs; treating an ID as a DMA pointer is unsafe. */
        host->malformed++;
        return;
    }

    if (endpoint == 1 && host->control_trbs[0])
    {
        /* Preserve the first terminal result. A later status event in the same
         * drain batch must not turn a malformed residual or stall into success. */
        if (host->control_complete)
        {
            return;
        }

        if (pointer != host->control_trbs[0] && pointer != host->control_trbs[1] &&
            pointer != host->control_trbs[2])
        {
            host->malformed++;
            return;
        }

        if (code != COMPLETION_SUCCESS && code != COMPLETION_SHORT)
        {
            host->control_code = code;
            host->control_complete = 1;
        }
        else if (pointer == host->control_trbs[1] && host->control_length)
        {
            if (residual > host->control_length)
            {
                host->control_code = 0;
                host->control_complete = 1;
                host->malformed++;
            }
            else
            {
                host->control_bytes = host->control_length - residual;
            }
        }
        else if (pointer == host->control_trbs[2])
        {
            host->control_code = code;
            host->control_complete = 1;
        }
    }
    else if (endpoint == host->endpoint && host->pending_interrupt)
    {
        if (pointer != host->pending_interrupt || residual > host->packet_bytes)
        {
            host->malformed++;
            host->interrupt_ready = 2;
        }
        else if (code == COMPLETION_SUCCESS || code == COMPLETION_SHORT)
        {
            host->interrupt_bytes = host->packet_bytes - residual;
            host->interrupt_ready = 1;
        }
        else
        {
            host->interrupt_ready = 2;
        }

        host->pending_interrupt = 0;
    }
}

static int drain_events(struct ls_xhci *host)
{
    for (unsigned int count = 0; count < LS_XHCI_EVENTS; ++count)
    {
        volatile struct ls_xhci_trb *source = &host->dma->event[host->event_dequeue];
        struct ls_xhci_trb event;
        unsigned int type;

        if ((source->control & 1) != host->event_cycle)
        {
            return LS_OK;
        }

        barrier(host);
        event.parameter_low = source->parameter_low;
        event.parameter_high = source->parameter_high;
        event.status = source->status;
        event.control = source->control;
        type = (event.control >> 10) & 0x3f;

        if (type == 33 && host->pending_command)
        {
            uint64_t pointer = (uint64_t)event.parameter_low | (uint64_t)event.parameter_high << 32;

            if (pointer == host->pending_command)
            {
                host->command_complete = 1;
                host->command_code = event.status >> 24;
                host->command_slot = event.control >> 24;
            }
            else
            {
                host->malformed++;
            }
        }
        else if (type == 32)
        {
            transfer_event(host, &event);
        }
        else if (type != 34)
        {
            return fail_controller(host, LS_IO_ERROR);
        }

        host->event_dequeue++;

        if (host->event_dequeue == LS_XHCI_EVENTS)
        {
            host->event_dequeue = 0;
            host->event_cycle ^= 1;
        }

        barrier(host);
        write64(host, host->runtime + 0x38,
                address(host, &host->dma->event[host->event_dequeue]) | 8);
    }

    /* A caller gets a bounded batch even if hardware continuously posts events. */
    return LS_OK;
}

static int command(struct ls_xhci *host, unsigned int type, uint64_t parameter,
                    unsigned int slot)
{
    uint64_t started;

    host->command_complete = 0;
    host->command_code = 0;
    host->pending_command = ring_submit(host, host->dma->command, &host->command,
                                        parameter, 0, TRB_TYPE(type) | slot << 24);
    host->commands++;
    write32(host, host->doorbell, 0);
    started = host->ops->now_ms(host->context);

    do
    {
        int result = drain_events(host);

        if (result)
        {
            return result;
        }

        if (host->command_complete)
        {
            host->pending_command = 0;
            return host->command_code == COMPLETION_SUCCESS ? LS_OK : LS_IO_ERROR;
        }

        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < WAIT_MS);

    host->timeouts++;
    return fail_controller(host, LS_TIMEOUT);
}

int ls_xhci_start(struct ls_xhci *host)
{
    uint32_t capability;
    uint32_t structural;
    unsigned int scratchpads;
    int result;

    if (!host || !host->ops || !host->dma || !host->ops->read32 || !host->ops->write32 ||
        !host->ops->barrier || !host->ops->now_ms || !host->ops->sleep_ms || !host->ops->motion ||
        host->bar_bytes < 0x1000 || host->bar_bytes > UINT32_MAX || host->running ||
        host->quarantined || ((uintptr_t)host->dma & 4095) || (host->dma_address & 4095) ||
        !host->dma_address || host->dma_address > UINT64_MAX - sizeof(*host->dma))
    {
        return LS_INVALID;
    }

    capability = read32(host, 0x10);
    structural = read32(host, 4);
    host->operational = read32(host, 0) & 0xff;
    host->ports = structural >> 24;
    host->runtime = read32(host, 0x18) & ~31U;
    host->doorbell = read32(host, 0x14) & ~3U;
    host->context_bytes = capability & 4 ? 64 : 32;
    scratchpads = read32(host, 8);
    scratchpads = ((scratchpads >> 27) & 31) | ((scratchpads >> 16) & 0x3e0);

    if (capability == UINT32_MAX || !host->ports || !(structural & 0xff) ||
        host->operational < 0x20 || host->operational + 0x400 + host->ports * 16 > host->bar_bytes ||
        host->runtime < 0x20 || host->runtime > host->bar_bytes - 0x40 ||
        host->doorbell < 0x20 || host->doorbell > host->bar_bytes - 8 ||
        scratchpads > LS_XHCI_SCRATCHPADS ||
        (!(capability & 1) && host->dma_address + sizeof(*host->dma) > UINT64_C(0x100000000)))
    {
        return LS_UNSUPPORTED;
    }

    memset(host->usb2_port, 0, sizeof(host->usb2_port));
    result = extended_capabilities(host, capability);

    if (result)
    {
        return result;
    }

    result = ls_xhci_stop(host);

    if (result)
    {
        return result;
    }

    write32(host, host->operational, 2);
    result = wait_register(host, host->operational, 2, 0);

    if (!result)
    {
        result = wait_register(host, host->operational + 4, 1U << 11, 0);
    }

    if (result || !(read32(host, host->operational + 8) & 1))
    {
        return fail_controller(host, result ? result : LS_UNSUPPORTED);
    }

    /* Hardware is stopped and reset before any inherited DMA address is replaced. */
    memset(host->dma, 0, sizeof(*host->dma));
    memset(host->inspected, 0, sizeof(host->inspected));
    host->slot = 0;
    host->port = 0;
    host->event_dequeue = 0;
    host->event_cycle = 1;
    host->pending_command = 0;
    host->pending_interrupt = 0;
    host->control_trbs[0] = 0;
    ring_initialize(host, host->dma->command, &host->command);
    ring_initialize(host, host->dma->ep0, &host->ep0);
    ring_initialize(host, host->dma->interrupt, &host->interrupt);

    for (unsigned int index = 0; index < scratchpads; ++index)
    {
        host->dma->scratch_pointer[index] = address(host, host->dma->scratch[index]);
    }

    if (scratchpads)
    {
        host->dma->dcbaa[0] = address(host, host->dma->scratch_pointer);
    }

    host->dma->erst[0] = address(host, host->dma->event);
    host->dma->erst[1] = address(host, host->dma->event) >> 32;
    host->dma->erst[2] = LS_XHCI_EVENTS;
    barrier(host);
    write64(host, host->operational + 0x30, address(host, host->dma->dcbaa));
    write64(host, host->operational + 0x18, address(host, host->dma->command) | 1);
    write32(host, host->operational + 0x38, 1);
    write32(host, host->runtime + 0x20, 0);
    write32(host, host->runtime + 0x28, 1);
    write64(host, host->runtime + 0x30, address(host, host->dma->erst));
    write64(host, host->runtime + 0x38, address(host, host->dma->event));
    write32(host, host->operational, 1);
    result = wait_register(host, host->operational + 4, 1, 0);

    if (result)
    {
        return fail_controller(host, result);
    }

    host->running = 1;
    return LS_OK;
}

static uint16_t little16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static int control_transfer(struct ls_xhci *host, uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, uint16_t bytes)
{
    uint64_t setup;
    uint64_t started;
    unsigned int input = !!(request_type & 0x80);
    uint32_t setup_control = TRB_TYPE(2) | TRB_IDT;

    if (!host->running || !host->slot || bytes > LS_XHCI_BUFFER_BYTES)
    {
        return LS_INVALID;
    }

    if (bytes)
    {
        setup_control |= (input ? 3U : 2U) << 16;
    }

    setup = request_type | (uint64_t)request << 8 | (uint64_t)value << 16 |
            (uint64_t)index << 32 | (uint64_t)bytes << 48;
    host->control_complete = 0;
    host->control_code = 0;
    host->control_length = bytes;
    host->control_bytes = bytes;
    host->control_trbs[0] = ring_submit(host, host->dma->ep0, &host->ep0,
                                       setup, 8, setup_control);
    host->control_trbs[1] = 0;

    if (bytes)
    {
        host->control_trbs[1] = ring_submit(host, host->dma->ep0, &host->ep0,
                                           address(host, host->dma->transfer), bytes,
                                           TRB_TYPE(3) | TRB_ISP | TRB_IOC |
                                           (input ? TRB_DIRECTION : 0));
    }

    host->control_trbs[2] = ring_submit(host, host->dma->ep0, &host->ep0, 0, 0,
                                       TRB_TYPE(4) | TRB_IOC |
                                       ((!bytes || !input) ? TRB_DIRECTION : 0));
    write32(host, host->doorbell + host->slot * 4, 1);
    started = host->ops->now_ms(host->context);

    do
    {
        int result = drain_events(host);

        if (result)
        {
            return result;
        }

        if (host->control_complete)
        {
            host->control_trbs[0] = 0;
            return host->control_code == COMPLETION_SUCCESS ? LS_OK : LS_IO_ERROR;
        }

        if (!(read32(host, host->operational + 0x400 + (host->port - 1) * 16) & PORT_CONNECTED))
        {
            /* Caller disables the slot before any transfer memory is reused. */
            return LS_IO_ERROR;
        }

        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < WAIT_MS);

    host->timeouts++;
    return fail_controller(host, LS_TIMEOUT);
}

static uint32_t *input_context(struct ls_xhci *host, unsigned int context)
{
    return (uint32_t *)(host->dma->input_context + context * host->context_bytes);
}

static uint32_t *output_context(struct ls_xhci *host, unsigned int context)
{
    return (uint32_t *)(host->dma->output_context + context * host->context_bytes);
}

static void endpoint_context(uint32_t *words, unsigned int type, unsigned int packet,
                              uint64_t ring)
{
    words[1] = packet << 16 | type << 3 | 3U << 1;
    words[2] = (uint32_t)ring | 1;
    words[3] = ring >> 32;
    words[4] = packet;
}

static int disable_slot(struct ls_xhci *host)
{
    int result = LS_OK;

    release_mouse(host);

    if (!host->running)
    {
        /* STOP may have failed. Even DCBAA writes are forbidden while DMA is
         * quarantined; only a later successful controller reset can reclaim it. */
        return LS_DISABLED;
    }

    if (host->slot)
    {
        result = command(host, 10, 0, host->slot);

        if (result)
        {
            return fail_controller(host, result);
        }
    }

    if (host->slot)
    {
        host->dma->dcbaa[host->slot] = 0;
    }

    host->slot = 0;
    host->pending_interrupt = 0;
    host->control_trbs[0] = 0;
    host->interrupt_ready = 0;
    barrier(host);
    return result;
}

static int reset_port(struct ls_xhci *host, unsigned int port)
{
    uint32_t offset = host->operational + 0x400 + (port - 1) * 16;
    uint32_t status = read32(host, offset);
    uint64_t started;

    if (!(status & PORT_CONNECTED))
    {
        return LS_DISABLED;
    }

    /* Preserve only writable power/wake policy; never echo PED or W1C changes. */
    status &= PORT_POWER | (7U << 25);
    write32(host, offset, status | PORT_POWER);
    host->ops->sleep_ms(host->context, 100);
    write32(host, offset, status | PORT_POWER | PORT_RESET);
    started = host->ops->now_ms(host->context);

    do
    {
        status = read32(host, offset);

        if (!(status & PORT_CONNECTED) || status == UINT32_MAX)
        {
            return LS_DISABLED;
        }

        if (!(status & PORT_RESET) && (status & PORT_ENABLED))
        {
            host->speed = (status >> 10) & 15;
            write32(host, offset, (status & (PORT_POWER | (7U << 25))) |
                    (status & PORT_CHANGES));
            host->ops->sleep_ms(host->context, 10);
            return host->speed >= 1 && host->speed <= 3 ? LS_OK : LS_UNSUPPORTED;
        }

        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < WAIT_MS);

    host->timeouts++;
    return LS_TIMEOUT;
}

static int address_device(struct ls_xhci *host)
{
    uint32_t *slot;
    uint32_t *endpoint;
    unsigned int packet = host->speed == 3 ? 64 : 8;
    int result = command(host, 9, 0, 0);

    if (result)
    {
        return result;
    }

    /* CONFIG.MaxSlotsEn is one, so any other ID is a broken completion. */
    if (host->command_slot != 1)
    {
        return fail_controller(host, LS_IO_ERROR);
    }

    host->slot = host->command_slot;
    memset(host->dma->input_context, 0, sizeof(host->dma->input_context));
    memset(host->dma->output_context, 0, sizeof(host->dma->output_context));
    ring_initialize(host, host->dma->ep0, &host->ep0);
    ring_initialize(host, host->dma->interrupt, &host->interrupt);
    host->dma->dcbaa[host->slot] = address(host, host->dma->output_context);
    input_context(host, 0)[1] = 3;
    slot = input_context(host, 1);
    slot[0] = (uint32_t)host->speed << 20 | 1U << 27;
    slot[1] = (uint32_t)host->port << 16;
    endpoint = input_context(host, 2);
    endpoint_context(endpoint, 4, packet, address(host, host->dma->ep0));
    barrier(host);
    result = command(host, 11, address(host, host->dma->input_context), host->slot);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, 0x80, 6, 0x100, 0, 8);

    if (result || host->control_bytes < 8)
    {
        return result ? result : LS_INVALID;
    }

    packet = host->dma->transfer[7];

    if (host->dma->transfer[0] != 18 || host->dma->transfer[1] != 1 ||
        (host->speed == 2 && packet != 8) || (host->speed == 3 && packet != 64) ||
        (host->speed == 1 && packet != 8 && packet != 16 && packet != 32 && packet != 64))
    {
        return LS_INVALID;
    }

    memset(host->dma->input_context, 0, sizeof(host->dma->input_context));
    input_context(host, 0)[1] = 2;
    endpoint = input_context(host, 2);
    memcpy(endpoint, output_context(host, 1), host->context_bytes);
    endpoint[1] = (endpoint[1] & 0xffff) | packet << 16;
    barrier(host);
    return command(host, 13, address(host, host->dma->input_context), host->slot);
}

static int find_mouse_interface(struct ls_xhci *host, unsigned int bytes,
                                 unsigned int *report_bytes, unsigned int *boot)
{
    const uint8_t *data = host->dma->transfer;
    unsigned int offset = 0;
    unsigned int candidate = 0;
    unsigned int found = 0;

    host->endpoint = 0;
    *report_bytes = 0;
    *boot = 0;

    while (offset < bytes)
    {
        unsigned int length;
        unsigned int type;

        if (bytes - offset < 2 || data[offset] < 2 || data[offset] > bytes - offset)
        {
            return LS_INVALID;
        }

        length = data[offset];
        type = data[offset + 1];

        if (type == 4)
        {
            if (length < 9)
            {
                return LS_INVALID;
            }

            if (candidate && host->endpoint && *report_bytes)
            {
                found = 1;
            }

            candidate = !found && data[offset + 3] == 0 && data[offset + 5] == 3 &&
                        data[offset + 7] != 1;

            if (candidate)
            {
                host->interface_number = data[offset + 2];
                *boot = data[offset + 6] == 1 && data[offset + 7] == 2;
                host->endpoint = 0;
                *report_bytes = 0;
            }
        }
        else if (candidate && type == 0x21)
        {
            if (length < 6 || data[offset + 5] > (length - 6) / 3)
            {
                return LS_INVALID;
            }

            for (unsigned int index = 0; index < data[offset + 5]; ++index)
            {
                if (data[offset + 6 + index * 3] == 0x22)
                {
                    *report_bytes = little16(&data[offset + 7 + index * 3]);
                }
            }
        }
        else if (candidate && type == 5)
        {
            unsigned int endpoint;
            unsigned int packet;

            if (length < 7)
            {
                return LS_INVALID;
            }

            endpoint = data[offset + 2];
            packet = little16(&data[offset + 4]);

            if ((endpoint & 0x80) && (endpoint & 15) && data[offset + 3] == 3)
            {
                if ((endpoint & 0x70) || !packet || packet > 64 ||
                    (host->speed == 2 && packet > 8) || !data[offset + 6])
                {
                    return LS_UNSUPPORTED;
                }

                host->endpoint = (endpoint & 15) * 2 + 1;
                host->packet_bytes = packet;
                host->interval = data[offset + 6];
            }
        }

        offset += length;
    }

    return host->endpoint && *report_bytes && *report_bytes <= LS_XHCI_BUFFER_BYTES ?
           LS_OK : LS_UNSUPPORTED;
}

static void submit_interrupt(struct ls_xhci *host)
{
    host->interrupt_ready = 0;
    host->pending_interrupt = ring_submit(host, host->dma->interrupt, &host->interrupt,
                                          address(host, host->dma->report), host->packet_bytes,
                                          TRB_TYPE(1) | TRB_IOC | TRB_ISP);
    write32(host, host->doorbell + host->slot * 4, host->endpoint);
}

static int configure_mouse(struct ls_xhci *host)
{
    unsigned int bytes;
    unsigned int report_bytes;
    unsigned int boot;
    unsigned int interval;
    uint32_t *slot;
    uint32_t *endpoint;
    int result = control_transfer(host, 0x80, 6, 0x100, 0, 18);

    if (result || host->control_bytes != 18)
    {
        return result ? result : LS_INVALID;
    }

    host->vendor = little16(&host->dma->transfer[8]);
    host->product = little16(&host->dma->transfer[10]);

    if (host->dma->transfer[4] == 9)
    {
        return LS_UNSUPPORTED;
    }

    result = control_transfer(host, 0x80, 6, 0x200, 0, 9);

    if (result || host->control_bytes != 9 || host->dma->transfer[0] != 9 ||
        host->dma->transfer[1] != 2)
    {
        return result ? result : LS_INVALID;
    }

    bytes = little16(&host->dma->transfer[2]);
    host->configuration = host->dma->transfer[5];

    if (bytes < 9 || bytes > LS_XHCI_BUFFER_BYTES || !host->configuration)
    {
        return LS_INVALID;
    }

    result = control_transfer(host, 0x80, 6, 0x200, 0, bytes);

    if (result || host->control_bytes != bytes)
    {
        return result ? result : LS_INVALID;
    }

    result = find_mouse_interface(host, bytes, &report_bytes, &boot);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, 0, 9, host->configuration, 0, 0);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, 0x81, 6, 0x2200, host->interface_number, report_bytes);

    if (result)
    {
        /* A stalled endpoint is not silently reused for an optimistic fallback. */
        return result;
    }

    if (ls_mouse_parse(&host->layout, host->dma->transfer, host->control_bytes))
    {
        if (!boot)
        {
            return LS_UNSUPPORTED;
        }

        result = control_transfer(host, 0x21, 11, 0, host->interface_number, 0);

        if (result)
        {
            return result;
        }

        ls_mouse_boot_layout(&host->layout);
    }

    for (unsigned int index = 0; index < host->layout.reports; ++index)
    {
        unsigned int required = (host->layout.bits[index] + 7) / 8 + host->layout.numbered;

        if (required > host->packet_bytes)
        {
            return LS_UNSUPPORTED;
        }
    }

    if (host->speed == 3)
    {
        if (host->interval > 16)
        {
            return LS_INVALID;
        }

        interval = host->interval - 1;
    }
    else
    {
        unsigned int frames = host->interval;

        interval = 3;

        while (frames > 1 && interval < 10)
        {
            frames >>= 1;
            interval++;
        }
    }

    memset(host->dma->input_context, 0, sizeof(host->dma->input_context));
    input_context(host, 0)[1] = 1U | 1U << host->endpoint;
    slot = input_context(host, 1);
    memcpy(slot, output_context(host, 0), host->context_bytes);
    slot[0] = (slot[0] & ~(31U << 27)) | (uint32_t)host->endpoint << 27;
    endpoint = input_context(host, host->endpoint + 1);
    endpoint_context(endpoint, 7, host->packet_bytes, address(host, host->dma->interrupt));
    endpoint[0] = interval << 16;
    endpoint[4] |= (uint32_t)host->packet_bytes << 16;
    barrier(host);
    result = command(host, 12, address(host, host->dma->input_context), host->slot);

    if (!result)
    {
        struct ls_mouse_motion initial = {0};

        initial.button_mask = 0xff;
        host->connected = 1;
        host->ops->motion(host->context, &initial, 1);
        submit_interrupt(host);
    }

    return result;
}

int ls_xhci_poll(struct ls_xhci *host)
{
    int result;
    uint32_t status;

    if (!host || !host->running || host->quarantined)
    {
        return LS_DISABLED;
    }

    status = read32(host, host->operational + 4);

    if (status == UINT32_MAX || (status & ((1U << 12) | (1U << 2) | 1U)))
    {
        return fail_controller(host, LS_IO_ERROR);
    }

    result = drain_events(host);

    if (result)
    {
        return result;
    }

    if (host->slot)
    {
        status = read32(host, host->operational + 0x400 + (host->port - 1) * 16);

        if (!(status & PORT_CONNECTED) || (status & (1U << 17)))
        {
            host->inspected[host->port] = 0;
            return disable_slot(host);
        }

        if (host->interrupt_ready == 2)
        {
            host->errors++;
            return disable_slot(host);
        }

        if (host->interrupt_ready == 1)
        {
            struct ls_mouse_motion motion;
            int decoded = ls_mouse_decode(&host->layout, host->dma->report,
                                           host->interrupt_bytes, &motion);

            if (!decoded)
            {
                host->reports++;
                host->ops->motion(host->context, &motion, 1);
            }
            else if (decoded < 0)
            {
                host->malformed++;
            }

            submit_interrupt(host);
        }

        return LS_OK;
    }

    for (unsigned int port = 1; port <= host->ports; ++port)
    {
        if (!host->usb2_port[port])
        {
            continue;
        }

        status = read32(host, host->operational + 0x400 + (port - 1) * 16);

        if (status == UINT32_MAX)
        {
            return fail_controller(host, LS_IO_ERROR);
        }

        if (!(status & PORT_CONNECTED))
        {
            host->inspected[port] = 0;
            continue;
        }

        if (host->inspected[port])
        {
            continue;
        }

        host->inspected[port] = 1;
        host->port = port;
        result = reset_port(host, port);

        if (!result)
        {
            result = address_device(host);
        }

        if (!result)
        {
            result = configure_mouse(host);
        }

        if (result)
        {
            host->errors += result != LS_UNSUPPORTED && result != LS_DISABLED;
            disable_slot(host);
        }

        /* At most one port enumeration per call; the adapter yields between calls. */
        return result;
    }

    return LS_OK;
}
