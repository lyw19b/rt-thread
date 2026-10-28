/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add bounded native xHCI mouse and HS camera transports
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
_Static_assert(offsetof(struct ls_xhci_device_dma, transfer) % 4096 == 0, "control buffer alignment");
_Static_assert(sizeof(struct ls_xhci_dma) % 4096 == 0, "DMA arena page extent");

_Static_assert(offsetof(struct ls_xhci_dma, iso) % 4096 == 0, "ISO ring alignment");
_Static_assert(sizeof(((struct ls_xhci_dma *)0)->iso) == 4096, "ISO ring segment size");
_Static_assert(offsetof(struct ls_xhci_dma, iso_payload) % 4096 == 0, "ISO payload alignment");
_Static_assert(LS_XHCI_ISO_TDS < LS_XHCI_ISO_USABLE, "retain unused ring space");

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

static void mirror_mouse(struct ls_xhci *host)
{
    struct ls_xhci_device *device = &host->devices[0];

    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        if (host->devices[index].kind == LS_XHCI_CLASS_MOUSE)
        {
            device = &host->devices[index];
            break;
        }
    }

    host->slot = device->slot;
    host->port = device->port;
    host->speed = device->speed;
    host->endpoint = device->endpoint;
    host->interface_number = device->interface_number;
    host->configuration = device->configuration;
    host->interval = device->interval;
    host->packet_bytes = device->packet_bytes;
    host->vendor = device->vendor;
    host->product = device->product;
    host->layout = device->layout;
    host->pending_interrupt = device->pending_interrupt;
    host->control_complete = device->control_complete;
    host->control_code = device->control_code;
    host->control_bytes = device->control_bytes;
    host->control_length = device->control_length;
    host->interrupt_ready = device->interrupt_ready;
    host->interrupt_bytes = device->interrupt_bytes;
    host->connected = device->kind == LS_XHCI_CLASS_MOUSE && device->connected;
    host->ep0 = device->ep0;
    host->interrupt = device->interrupt;
    memcpy(host->control_trbs, device->control_trbs, sizeof(host->control_trbs));
}

static void release_mouse(struct ls_xhci *host, struct ls_xhci_device *device)
{
    if (device->connected && device->kind == LS_XHCI_CLASS_MOUSE)
    {
        struct ls_mouse_motion released = {0};

        released.button_mask = 0xff;
        device->connected = 0;
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
    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        struct ls_xhci_device *device = &host->devices[index];

        if (device->connected)
        {
            device->generation++;
        }

        release_mouse(host, device);
        device->connected = 0;
    }

    host->connected = 0;
    host->iso.running = 0;
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
    mirror_mouse(host);
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

static void device_transfer_event(struct ls_xhci *host, struct ls_xhci_device *device, const struct ls_xhci_trb *event)
{
    uint64_t pointer = (uint64_t)event->parameter_low | (uint64_t)event->parameter_high << 32;
    unsigned int endpoint = (event->control >> 16) & 0x1f;
    unsigned int code = event->status >> 24;
    unsigned int residual = event->status & 0xffffff;

    if ((event->control >> 24) != device->slot || !device->slot)
    {
        return;
    }

    if (event->control & (1U << 2))
    {
        /* We never submit Event Data TRBs; treating an ID as a DMA pointer is unsafe. */
        host->malformed++;
        return;
    }

    if (endpoint == 1 && device->control_trbs[0])
    {
        /* Preserve the first terminal result. A later status event in the same
         * drain batch must not turn a malformed residual or stall into success. */
        if (device->control_complete)
        {
            return;
        }

        if (pointer != device->control_trbs[0] && pointer != device->control_trbs[1] &&
            pointer != device->control_trbs[2])
        {
            host->malformed++;
            return;
        }

        if (code != COMPLETION_SUCCESS && code != COMPLETION_SHORT)
        {
            device->control_code = code;
            device->control_complete = 1;
        }
        else if (pointer == device->control_trbs[1] && device->control_length)
        {
            if (residual > device->control_length)
            {
                device->control_code = 0;
                device->control_complete = 1;
                host->malformed++;
            }
            else
            {
                device->control_bytes = device->control_length - residual;
            }
        }
        else if (pointer == device->control_trbs[2])
        {
            device->control_code = code;
            device->control_complete = 1;
        }
    }
    else if (endpoint == device->endpoint && device->pending_interrupt)
    {
        if (pointer != device->pending_interrupt || residual > device->packet_bytes)
        {
            host->malformed++;
            device->interrupt_ready = 2;
        }
        else if (code == COMPLETION_SUCCESS || code == COMPLETION_SHORT)
        {
            device->interrupt_bytes = device->packet_bytes - residual;
            device->interrupt_ready = 1;
        }
        else
        {
            device->interrupt_ready = 2;
        }

        device->pending_interrupt = 0;
    }
}

static void iso_event(struct ls_xhci *host, const struct ls_xhci_trb *event);

static void transfer_event(struct ls_xhci *host, const struct ls_xhci_trb *event)
{
    unsigned int slot = event->control >> 24;

    if (host->iso.device && slot == host->iso.device->slot &&
        ((event->control >> 16) & 31) == host->iso.endpoint)
    {
        iso_event(host, event);
        return;
    }

    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        if (slot && host->devices[index].slot == slot)
        {
            device_transfer_event(host, &host->devices[index], event);
            return;
        }
    }
}

static void service_mouse_reports(struct ls_xhci *host);

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

            if (!host->running)
            {
                return LS_IO_ERROR;
            }
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

static int command_endpoint(struct ls_xhci *host, unsigned int type, uint64_t parameter,
                             unsigned int slot, unsigned int endpoint)
{
    uint64_t started;

    host->command_complete = 0;
    host->command_code = 0;
    host->pending_command = ring_submit(host, host->dma->command, &host->command,
                                        parameter, 0, TRB_TYPE(type) | slot << 24 | endpoint << 16);
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

        service_mouse_reports(host);

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

static int command(struct ls_xhci *host, unsigned int type, uint64_t parameter,
                    unsigned int slot)
{
    return command_endpoint(host, type, parameter, slot, 0);
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
    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        uint32_t generation = host->devices[index].generation + 1;

        memset(&host->devices[index], 0, sizeof(host->devices[index]));
        host->devices[index].dma = &host->dma->device[index];
        host->devices[index].generation = generation;
    }

    memset(&host->iso, 0, sizeof(host->iso));
    host->max_slots = read32(host, 4) & 0xff;
    if (host->max_slots > LS_XHCI_DEVICES)
    {
        host->max_slots = LS_XHCI_DEVICES;
    }

    if (!host->max_slots)
    {
        return fail_controller(host, LS_UNSUPPORTED);
    }

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
    write32(host, host->operational + 0x38, host->max_slots);
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

static int control_transfer(struct ls_xhci *host, struct ls_xhci_device *device, uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, uint16_t bytes, unsigned int timeout_ms)
{
    uint64_t setup;
    uint64_t started;
    unsigned int input = !!(request_type & 0x80);
    uint32_t setup_control = TRB_TYPE(2) | TRB_IDT;

    if (!host->running || !device->slot || bytes > LS_XHCI_BUFFER_BYTES)
    {
        return LS_INVALID;
    }

    if (bytes)
    {
        setup_control |= (input ? 3U : 2U) << 16;
    }

    setup = request_type | (uint64_t)request << 8 | (uint64_t)value << 16 |
            (uint64_t)index << 32 | (uint64_t)bytes << 48;
    device->control_complete = 0;
    device->control_code = 0;
    device->control_length = bytes;
    device->control_bytes = bytes;
    device->control_trbs[0] = ring_submit(host, device->dma->ep0, &device->ep0,
                                       setup, 8, setup_control);
    device->control_trbs[1] = 0;

    if (bytes)
    {
        device->control_trbs[1] = ring_submit(host, device->dma->ep0, &device->ep0,
                                           address(host, device->dma->transfer), bytes,
                                           TRB_TYPE(3) | TRB_ISP | TRB_IOC |
                                           (input ? TRB_DIRECTION : 0));
    }

    device->control_trbs[2] = ring_submit(host, device->dma->ep0, &device->ep0, 0, 0,
                                       TRB_TYPE(4) | TRB_IOC |
                                       ((!bytes || !input) ? TRB_DIRECTION : 0));
    write32(host, host->doorbell + device->slot * 4, 1);
    started = host->ops->now_ms(host->context);

    do
    {
        int result = drain_events(host);

        if (result)
        {
            return result;
        }

        service_mouse_reports(host);

        if (device->control_complete)
        {
            device->control_trbs[0] = 0;
            return device->control_code == COMPLETION_SUCCESS ? LS_OK : LS_IO_ERROR;
        }

        if (!(read32(host, host->operational + 0x400 + (device->port - 1) * 16) & PORT_CONNECTED))
        {
            /* Caller disables the slot before any transfer memory is reused. */
            return LS_IO_ERROR;
        }

        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < timeout_ms);

    host->timeouts++;
    return fail_controller(host, LS_TIMEOUT);
}

static uint32_t *input_context(struct ls_xhci *host, struct ls_xhci_device *device, unsigned int context)
{
    return (uint32_t *)(device->dma->input_context + context * host->context_bytes);
}

static uint32_t *output_context(struct ls_xhci *host, struct ls_xhci_device *device, unsigned int context)
{
    return (uint32_t *)(device->dma->output_context + context * host->context_bytes);
}

static void endpoint_context(uint32_t *words, unsigned int type, unsigned int packet,
                              uint64_t ring)
{
    words[1] = packet << 16 | type << 3 | 3U << 1;
    words[2] = (uint32_t)ring | 1;
    words[3] = ring >> 32;
    words[4] = packet;
}

static int disable_slot(struct ls_xhci *host, struct ls_xhci_device *device)
{
    int result = LS_OK;

    release_mouse(host, device);
    device->connected = 0;
    device->generation++;

    if (host->iso.device == device)
    {
        host->iso.running = 0;
        host->iso.stopping = 1;
    }

    if (!host->running)
    {
        /* STOP may have failed. Even DCBAA writes are forbidden while DMA is
         * quarantined; only a later successful controller reset can reclaim it. */
        return LS_DISABLED;
    }

    if (device->slot)
    {
        result = command(host, 10, 0, device->slot);

        if (result)
        {
            return fail_controller(host, result);
        }
    }

    if (device->slot)
    {
        host->dma->dcbaa[device->slot] = 0;
    }

    if (host->iso.device == device)
    {
        host->iso.device = NULL;
        host->iso.stopping = 0;
        host->iso.count = 0;
    }

    device->slot = 0;
    device->pending_interrupt = 0;
    device->control_trbs[0] = 0;
    device->interrupt_ready = 0;
    barrier(host);
    return result;
}

static int service_delay(struct ls_xhci *host, unsigned int milliseconds)
{
    uint64_t started = host->ops->now_ms(host->context);

    do
    {
        int result = drain_events(host);

        if (result || !host->running)
        {
            return result ? result : LS_DISABLED;
        }

        service_mouse_reports(host);
        host->ops->sleep_ms(host->context, 1);
    } while (host->ops->now_ms(host->context) - started < milliseconds);

    return LS_OK;
}

static int reset_port(struct ls_xhci *host, struct ls_xhci_device *device, unsigned int port)
{
    uint32_t offset = host->operational + 0x400 + (port - 1) * 16;
    uint32_t status = read32(host, offset);
    uint64_t started;
    int result;

    if (!(status & PORT_CONNECTED))
    {
        return LS_DISABLED;
    }

    /* Preserve only writable power/wake policy; never echo PED or W1C changes. */
    status &= PORT_POWER | (7U << 25);
    write32(host, offset, status | PORT_POWER);
    if (!(status & PORT_POWER))
    {
        result = service_delay(host, 100);

        if (result)
        {
            return result;
        }
    }
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
            device->speed = (status >> 10) & 15;
            write32(host, offset, (status & (PORT_POWER | (7U << 25))) |
                    (status & PORT_CHANGES));
            result = service_delay(host, 10);

            if (result)
            {
                return result;
            }
            return device->speed >= 1 && device->speed <= 3 ? LS_OK : LS_UNSUPPORTED;
        }

        result = service_delay(host, 1);

        if (result)
        {
            return result;
        }
    } while (host->ops->now_ms(host->context) - started < WAIT_MS);

    host->timeouts++;
    return LS_TIMEOUT;
}

static int address_device(struct ls_xhci *host, struct ls_xhci_device *device)
{
    uint32_t *slot;
    uint32_t *endpoint;
    unsigned int packet = device->speed == 3 ? 64 : 8;
    int result = command(host, 9, 0, 0);

    if (result)
    {
        return result;
    }

    /* Validate returned slot identity before indexing the shared DCBAA. */
    if (!host->command_slot || host->command_slot > host->max_slots ||
        host->dma->dcbaa[host->command_slot])
    {
        return fail_controller(host, LS_IO_ERROR);
    }

    device->slot = host->command_slot;
    memset(device->dma->input_context, 0, sizeof(device->dma->input_context));
    memset(device->dma->output_context, 0, sizeof(device->dma->output_context));
    ring_initialize(host, device->dma->ep0, &device->ep0);
    ring_initialize(host, device->dma->interrupt, &device->interrupt);
    host->dma->dcbaa[device->slot] = address(host, device->dma->output_context);
    input_context(host, device, 0)[1] = 3;
    slot = input_context(host, device, 1);
    slot[0] = (uint32_t)device->speed << 20 | 1U << 27;
    slot[1] = (uint32_t)device->port << 16;
    endpoint = input_context(host, device, 2);
    endpoint_context(endpoint, 4, packet, address(host, device->dma->ep0));
    barrier(host);
    result = command(host, 11, address(host, device->dma->input_context), device->slot);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, device, 0x80, 6, 0x100, 0, 8, WAIT_MS);

    if (result || device->control_bytes < 8)
    {
        return result ? result : LS_INVALID;
    }

    packet = device->dma->transfer[7];

    if (device->dma->transfer[0] != 18 || device->dma->transfer[1] != 1 ||
        (device->speed == 2 && packet != 8) || (device->speed == 3 && packet != 64) ||
        (device->speed == 1 && packet != 8 && packet != 16 && packet != 32 && packet != 64))
    {
        return LS_INVALID;
    }

    memset(device->dma->input_context, 0, sizeof(device->dma->input_context));
    input_context(host, device, 0)[1] = 2;
    endpoint = input_context(host, device, 2);
    memcpy(endpoint, output_context(host, device, 1), host->context_bytes);
    endpoint[1] = (endpoint[1] & 0xffff) | packet << 16;
    barrier(host);
    return command(host, 13, address(host, device->dma->input_context), device->slot);
}

static int find_mouse_interface(struct ls_xhci *host, struct ls_xhci_device *device, unsigned int bytes,
                                 unsigned int *report_bytes, unsigned int *boot)
{
    const uint8_t *data = device->dma->transfer;

    (void)host;
    unsigned int offset = 0;
    unsigned int candidate = 0;
    unsigned int found = 0;

    device->endpoint = 0;
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

            if (candidate && device->endpoint && *report_bytes)
            {
                found = 1;
            }

            candidate = !found && data[offset + 3] == 0 && data[offset + 5] == 3 &&
                        data[offset + 7] != 1;

            if (candidate)
            {
                device->interface_number = data[offset + 2];
                *boot = data[offset + 6] == 1 && data[offset + 7] == 2;
                device->endpoint = 0;
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
                    (device->speed == 2 && packet > 8) || !data[offset + 6])
                {
                    return LS_UNSUPPORTED;
                }

                device->endpoint = (endpoint & 15) * 2 + 1;
                device->packet_bytes = packet;
                device->interval = data[offset + 6];
            }
        }

        offset += length;
    }

    return device->endpoint && *report_bytes && *report_bytes <= LS_XHCI_BUFFER_BYTES ?
           LS_OK : LS_UNSUPPORTED;
}

static void submit_interrupt(struct ls_xhci *host, struct ls_xhci_device *device)
{
    device->interrupt_ready = 0;
    device->pending_interrupt = ring_submit(host, device->dma->interrupt, &device->interrupt,
                                          address(host, device->dma->report), device->packet_bytes,
                                          TRB_TYPE(1) | TRB_IOC | TRB_ISP);
    write32(host, host->doorbell + device->slot * 4, device->endpoint);
}

static int enumerate_device(struct ls_xhci *host, struct ls_xhci_device *device)
{
    unsigned int bytes;
    int result = control_transfer(host, device, 0x80, 6, 0x100, 0, 18, WAIT_MS);

    if (result || device->control_bytes != 18)
    {
        return result ? result : LS_INVALID;
    }

    memcpy(device->device_descriptor, device->dma->transfer, 18);
    device->vendor = little16(&device->dma->transfer[8]);
    device->product = little16(&device->dma->transfer[10]);

    if (device->dma->transfer[4] == 9)
    {
        return LS_UNSUPPORTED;
    }

    result = control_transfer(host, device, 0x80, 6, 0x200, 0, 9, WAIT_MS);

    if (result || device->control_bytes != 9 || device->dma->transfer[0] != 9 ||
        device->dma->transfer[1] != 2)
    {
        return result ? result : LS_INVALID;
    }

    bytes = little16(&device->dma->transfer[2]);
    device->configuration = device->dma->transfer[5];

    if (bytes < 9 || bytes > LS_XHCI_BUFFER_BYTES || !device->configuration)
    {
        return LS_INVALID;
    }

    result = control_transfer(host, device, 0x80, 6, 0x200, 0, bytes, WAIT_MS);

    if (result || device->control_bytes != bytes)
    {
        return result ? result : LS_INVALID;
    }

    device->raw_config_length = bytes;
    memcpy(device->raw_config, device->dma->transfer, bytes);
    return LS_OK;
}

static int configure_mouse(struct ls_xhci *host, struct ls_xhci_device *device)
{
    unsigned int bytes = device->raw_config_length;
    unsigned int report_bytes, boot, interval;
    uint32_t *slot, *endpoint;
    int result;

    memcpy(device->dma->transfer, device->raw_config, bytes);
    result = find_mouse_interface(host, device, bytes, &report_bytes, &boot);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, device, 0, 9, device->configuration, 0, 0, WAIT_MS);

    if (result)
    {
        return result;
    }

    result = control_transfer(host, device, 0x81, 6, 0x2200, device->interface_number, report_bytes, WAIT_MS);

    if (result)
    {
        /* A stalled endpoint is not silently reused for an optimistic fallback. */
        return result;
    }

    if (ls_mouse_parse(&device->layout, device->dma->transfer, device->control_bytes))
    {
        if (!boot)
        {
            return LS_UNSUPPORTED;
        }

        result = control_transfer(host, device, 0x21, 11, 0, device->interface_number, 0, WAIT_MS);

        if (result)
        {
            return result;
        }

        ls_mouse_boot_layout(&device->layout);
    }

    for (unsigned int index = 0; index < device->layout.reports; ++index)
    {
        unsigned int required = (device->layout.bits[index] + 7) / 8 + device->layout.numbered;

        if (required > device->packet_bytes)
        {
            return LS_UNSUPPORTED;
        }
    }

    if (device->speed == 3)
    {
        if (device->interval > 16)
        {
            return LS_INVALID;
        }

        interval = device->interval - 1;
    }
    else
    {
        unsigned int frames = device->interval;

        interval = 3;

        while (frames > 1 && interval < 10)
        {
            frames >>= 1;
            interval++;
        }
    }

    memset(device->dma->input_context, 0, sizeof(device->dma->input_context));
    input_context(host, device, 0)[1] = 1U | 1U << device->endpoint;
    slot = input_context(host, device, 1);
    memcpy(slot, output_context(host, device, 0), host->context_bytes);
    slot[0] = (slot[0] & ~(31U << 27)) | (uint32_t)device->endpoint << 27;
    endpoint = input_context(host, device, device->endpoint + 1);
    endpoint_context(endpoint, 7, device->packet_bytes, address(host, device->dma->interrupt));
    endpoint[0] = interval << 16;
    endpoint[4] |= (uint32_t)device->packet_bytes << 16;
    barrier(host);
    result = command(host, 12, address(host, device->dma->input_context), device->slot);

    if (!result)
    {
        struct ls_mouse_motion initial = {0};

        initial.button_mask = 0xff;
        device->kind = LS_XHCI_CLASS_MOUSE;
        device->connected = 1;
        host->ops->motion(host->context, &initial, 1);
        submit_interrupt(host, device);
    }

    return result;
}


static int valid_device(struct ls_xhci *host, struct ls_xhci_device *device)
{
    if (!host || !device || !host->running || host->quarantined)
    {
        return 0;
    }

    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        if (&host->devices[index] == device)
        {
            return device->slot && device->connected;
        }
    }

    return 0;
}

static void service_mouse_reports(struct ls_xhci *host)
{
    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        struct ls_xhci_device *device = &host->devices[index];

        if (device->kind == LS_XHCI_CLASS_MOUSE && device->connected &&
            device->interrupt_ready == 1)
        {
            struct ls_mouse_motion motion;
            int decoded = ls_mouse_decode(&device->layout, device->dma->report,
                                           device->interrupt_bytes, &motion);

            if (!decoded)
            {
                host->reports++;
                host->ops->motion(host->context, &motion, 1);
            }
            else if (decoded < 0)
            {
                host->malformed++;
            }

            submit_interrupt(host, device);
        }
    }
}

static int camera_interface(struct ls_xhci_device *device)
{
    unsigned int offset = 0;
    unsigned int found = 0;

    while (offset < device->raw_config_length)
    {
        const uint8_t *descriptor = &device->raw_config[offset];
        unsigned int remaining = device->raw_config_length - offset;
        unsigned int length;

        if (remaining < 2 || descriptor[0] < 2 || descriptor[0] > remaining)
        {
            return LS_INVALID;
        }

        length = descriptor[0];

        if (descriptor[1] == 4)
        {
            if (length < 9)
            {
                return LS_INVALID;
            }

            if (descriptor[3] == 0 && descriptor[5] == 14 && descriptor[6] == 1)
            {
                device->interface_number = descriptor[2];
                found++;
            }
        }

        offset += length;
    }

    return found == 1 && device->speed == 3 ? LS_OK : LS_UNSUPPORTED;
}

int ls_xhci_poll(struct ls_xhci *host)
{
    uint32_t status;
    int result;

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

    service_mouse_reports(host);

    for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
    {
        struct ls_xhci_device *device = &host->devices[index];

        if (!device->slot)
        {
            continue;
        }

        status = read32(host, host->operational + 0x400 + (device->port - 1) * 16);

        if (!(status & PORT_CONNECTED) || (status & (1U << 17)) ||
            device->interrupt_ready == 2)
        {
            if (device->interrupt_ready == 2)
            {
                host->errors++;
            }

            host->inspected[device->port] = 0;
            result = disable_slot(host, device);
            mirror_mouse(host);
            return result;
        }
    }

    for (unsigned int port = 1; port <= host->ports; port++)
    {
        struct ls_xhci_device *device = NULL;
        unsigned int occupied = 0;

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

        for (unsigned int index = 0; index < LS_XHCI_DEVICES; index++)
        {
            if (host->devices[index].slot)
            {
                occupied++;
            }
            else if (!device)
            {
                device = &host->devices[index];
            }
        }

        if (!device || occupied >= host->max_slots)
        {
            break;
        }

        host->inspected[port] = 1;
        device->port = port;
        device->kind = 0;
        device->connected = 0;
        device->generation++;
        device->raw_config_length = 0;
        result = reset_port(host, device, port);

        if (!result)
        {
            result = address_device(host, device);
        }

        if (!result)
        {
            result = enumerate_device(host, device);
        }

        if (!result)
        {
            if (!camera_interface(device))
            {
                result = control_transfer(host, device, 0, 9,
                                           device->configuration, 0, 0, WAIT_MS);

                if (!result)
                {
                    device->kind = LS_XHCI_CLASS_CAMERA;
                    device->connected = 1;
                }
            }
            else
            {
                result = configure_mouse(host, device);
            }
        }

        if (result && host->running)
        {
            int cleanup = disable_slot(host, device);

            if (cleanup)
            {
                result = cleanup;
            }
        }

        mirror_mouse(host);
        return result;
    }

    mirror_mouse(host);
    return LS_OK;
}

int ls_xhci_control(struct ls_xhci *host, struct ls_xhci_device *device,
                    uint8_t request_type, uint8_t request, uint16_t value,
                    uint16_t index, void *data, uint16_t length,
                    unsigned int timeout_ms)
{
    int result;

    if (!valid_device(host, device))
    {
        return LS_DISABLED;
    }

    if ((!data && length) || length > LS_XHCI_BUFFER_BYTES ||
        !timeout_ms || timeout_ms > WAIT_MS)
    {
        return LS_INVALID;
    }

    if (!(request_type & 0x80) && length)
    {
        memcpy(device->dma->transfer, data, length);
    }

    result = control_transfer(host, device, request_type, request, value,
                               index, length, timeout_ms);

    if (result)
    {
        /* Never reuse an EP0 halted by an optional UVC request without a
         * confirmed reset and a new dequeue pointer. */
        if (host->running && device->control_complete && device->control_code == 6)
        {
            int recovery = command_endpoint(host, 14, 0, device->slot, 1);

            if (!recovery)
            {
                ring_initialize(host, device->dma->ep0, &device->ep0);
                recovery = command_endpoint(host, 16,
                    address(host, device->dma->ep0) | 1, device->slot, 1);
            }

            if (recovery)
            {
                return fail_controller(host, recovery);
            }
        }
        else if (host->running)
        {
            disable_slot(host, device);
        }

        return result;
    }

    if ((request_type & 0x80) && device->control_bytes)
    {
        memcpy(data, device->dma->transfer, device->control_bytes);
    }

    return device->control_bytes;
}

static void iso_submit(struct ls_xhci *host)
{
    struct ls_xhci_iso *iso = &host->iso;
    unsigned int index = iso->enqueue;
    struct ls_xhci_trb *trb = &host->dma->iso[index];
    unsigned int buffer = iso->payload_enqueue;
    uint64_t physical = address(host, host->dma->iso_payload[buffer]);
    unsigned int packets = iso->capacity / iso->packet_bytes;

    trb->parameter_low = physical;
    trb->parameter_high = physical >> 32;
    trb->status = iso->capacity;
    iso->pending[index] = address(host, trb);
    iso->buffer_index[index] = (uint8_t)buffer;
    iso->payload_enqueue = (buffer + 1) % LS_XHCI_ISO_TDS;
    barrier(host);
    *(volatile uint32_t *)&trb->control = TRB_TYPE(5) | TRB_IOC | TRB_ISP |
                                        (1U << 31) | ((packets - 1) << 16) | iso->cycle;
    iso->enqueue++;

    if (iso->enqueue == LS_XHCI_ISO_USABLE)
    {
        barrier(host);
        *(volatile uint32_t *)&host->dma->iso[LS_XHCI_ISO_USABLE].control =
            TRB_TYPE(6) | 2 | iso->cycle;
        iso->enqueue = 0;
        iso->cycle ^= 1;
    }

    barrier(host);
}

static void iso_queue(struct ls_xhci *host, int result, const uint8_t *data)
{
    struct ls_xhci_iso *iso = &host->iso;
    unsigned int tail;

    if (iso->count == LS_XHCI_ISO_QUEUE)
    {
        /* Discard the old backlog and place the error before any subsequent
         * payload. Marking only its oldest entry would put the error before
         * still-queued data and could let the assembler resynchronize too early. */
        iso->dropped += iso->count + 1;
        iso->head = 0;
        iso->count = 1;
        iso->queue[0].result = LS_IO_ERROR;
        return;
    }

    tail = (iso->head + iso->count) % LS_XHCI_ISO_QUEUE;
    iso->queue[tail].result = result;

    if (result > 0)
    {
        memcpy(iso->queue[tail].bytes, data, (size_t)result);
    }

    iso->count++;
}

static void iso_event(struct ls_xhci *host, const struct ls_xhci_trb *event)
{
    struct ls_xhci_iso *iso = &host->iso;
    uint64_t pointer = (uint64_t)event->parameter_low | (uint64_t)event->parameter_high << 32;
    uint64_t first = address(host, host->dma->iso);
    unsigned int code = event->status >> 24;
    unsigned int residual = event->status & 0xffffff;
    unsigned int index;
    int result;

    if (!iso->running || iso->stopping)
    {
        return;
    }

    if (code == 14 || code == 15)
    {
        iso->errors++;
        iso_queue(host, LS_IO_ERROR, NULL);
        return;
    }

    if ((event->control & 4) || pointer < first ||
        pointer >= first + LS_XHCI_ISO_USABLE * sizeof(struct ls_xhci_trb) || (pointer & 15))
    {
        host->malformed++;
        iso->errors++;
        fail_controller(host, LS_IO_ERROR);
        return;
    }

    index = (unsigned int)((pointer - first) / sizeof(struct ls_xhci_trb));

    if (iso->pending[index] != pointer || residual > iso->capacity)
    {
        host->malformed++;
        iso->errors++;
        fail_controller(host, LS_IO_ERROR);
        return;
    }

    iso->pending[index] = 0;

    result = code == COMPLETION_SUCCESS || code == COMPLETION_SHORT ?
             (int)(iso->capacity - residual) : LS_IO_ERROR;

    if (result < 0)
    {
        iso->errors++;
        iso->missed += code == 23;
    }
    else
    {
        iso->packets++;
    }

    iso_queue(host, result, host->dma->iso_payload[iso->buffer_index[index]]);

    /* xHCI consumes a transfer ring in order. Do not overwrite a different
     * still-owned TRB if a corrupt/out-of-order event violates that ownership. */
    if (index != (iso->enqueue + LS_XHCI_ISO_USABLE - LS_XHCI_ISO_TDS) % LS_XHCI_ISO_USABLE)
    {
        fail_controller(host, LS_IO_ERROR);
        return;
    }

    iso_submit(host);
    write32(host, host->doorbell + iso->device->slot * 4, iso->endpoint);
}

int ls_xhci_iso_start(struct ls_xhci *host, struct ls_xhci_device *device,
                      uint8_t endpoint, uint16_t packet, uint8_t interval)
{
    struct ls_xhci_iso *iso;
    uint32_t *slot;
    uint32_t *context;
    unsigned int burst = (packet >> 11) & 3;
    unsigned int maximum = packet & 0x7ff;
    unsigned int dci = (endpoint & 15) * 2 + 1;
    uint64_t ring;
    int result;

    if (!valid_device(host, device))
    {
        return LS_DISABLED;
    }

    if (device->speed != 3 || device->kind != LS_XHCI_CLASS_CAMERA ||
        !(endpoint & 0x80) || !(endpoint & 15) || (endpoint & 0x70) ||
        (packet & 0xe000) || !maximum || maximum > 1024 || burst > 2 ||
        interval < 1 || interval > 4)
    {
        return LS_UNSUPPORTED;
    }

    if (host->iso.device)
    {
        return LS_BUSY;
    }

    iso = &host->iso;
    memset(iso, 0, sizeof(*iso));
    iso->device = device;
    iso->endpoint = dci;
    iso->packet_bytes = maximum;
    iso->capacity = maximum * (burst + 1);
    iso->interval = interval;
    iso->cycle = 1;
    memset(host->dma->iso, 0, sizeof(host->dma->iso));
    ring = address(host, host->dma->iso);
    host->dma->iso[LS_XHCI_ISO_USABLE].parameter_low = ring;
    host->dma->iso[LS_XHCI_ISO_USABLE].parameter_high = ring >> 32;
    host->dma->iso[LS_XHCI_ISO_USABLE].control = TRB_TYPE(6) | 2;
    memset(device->dma->input_context, 0, sizeof(device->dma->input_context));
    input_context(host, device, 0)[1] = 1U | 1U << dci;
    slot = input_context(host, device, 1);
    memcpy(slot, output_context(host, device, 0), host->context_bytes);
    slot[0] = (slot[0] & ~(31U << 27)) | dci << 27;
    context = input_context(host, device, dci + 1);
    endpoint_context(context, 5, maximum, ring);
    context[0] = (uint32_t)(interval - 1) << 16;
    context[1] = (context[1] & ~6U) | burst << 8;
    context[4] = iso->capacity | iso->capacity << 16;
    barrier(host);
    result = command(host, 12, address(host, device->dma->input_context), device->slot);

    if (result)
    {
        /* Resource/Bandwidth Error is a completed admission rejection. No ISO
         * TD has been published or doorbell rung, so the mouse can keep running.
         * Uncertain controller state still follows the stop/quarantine path. */
        if (host->running && host->command_complete &&
            (host->command_code == 7 || host->command_code == 8))
        {
            iso->device = NULL;
            return LS_BUSY;
        }

        return fail_controller(host, result);
    }

    for (unsigned int index = 0; index < LS_XHCI_ISO_TDS; index++)
    {
        iso_submit(host);
    }

    iso->running = 1;
    write32(host, host->doorbell + device->slot * 4, dci);
    return LS_OK;
}

int ls_xhci_iso_read(struct ls_xhci *host, struct ls_xhci_device *device,
                     uint8_t *data, size_t capacity)
{
    struct ls_xhci_iso *iso;
    struct ls_xhci_iso_packet *packet;
    int result;

    if (!valid_device(host, device) || host->iso.device != device || !host->iso.running)
    {
        return LS_DISABLED;
    }

    if (!data || !capacity)
    {
        return LS_INVALID;
    }

    iso = &host->iso;

    if (!iso->count)
    {
        return LS_AGAIN;
    }

    packet = &iso->queue[iso->head];
    result = packet->result;

    if (result > 0 && (size_t)result > capacity)
    {
        return LS_INVALID;
    }

    if (result > 0)
    {
        memcpy(data, packet->bytes, (size_t)result);
    }

    iso->head = (iso->head + 1) % LS_XHCI_ISO_QUEUE;
    iso->count--;
    return result;
}

int ls_xhci_iso_stop(struct ls_xhci *host, struct ls_xhci_device *device)
{
    struct ls_xhci_iso *iso;
    uint32_t *slot;
    int result;

    if (!host || !device)
    {
        return LS_INVALID;
    }

    iso = &host->iso;

    if (!iso->device)
    {
        return LS_OK;
    }

    if (iso->device != device)
    {
        return LS_BUSY;
    }

    if (!host->running || host->quarantined)
    {
        return LS_DISABLED;
    }

    iso->stopping = 1;
    iso->running = 0;
    result = command_endpoint(host, 15, 0, device->slot, iso->endpoint);

    if (result)
    {
        return fail_controller(host, result);
    }

    memset(device->dma->input_context, 0, sizeof(device->dma->input_context));
    input_context(host, device, 0)[0] = 1U << iso->endpoint;
    input_context(host, device, 0)[1] = 1;
    slot = input_context(host, device, 1);
    memcpy(slot, output_context(host, device, 0), host->context_bytes);
    slot[0] = (slot[0] & ~(31U << 27)) | 1U << 27;
    result = command(host, 12, address(host, device->dma->input_context), device->slot);

    if (result)
    {
        return fail_controller(host, result);
    }

    iso->device = NULL;
    iso->stopping = 0;
    iso->count = 0;
    memset(iso->pending, 0, sizeof(iso->pending));
    return LS_OK;
}
