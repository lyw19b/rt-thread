/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Bind firmware-assigned Loongson GMAC2 to lwIP through bounded polling.
 */

#include <rtthread.h>
#include <rthw.h>
#include <netif/ethernetif.h>
#include <lwip/pbuf.h>
#include "ls2k3000_pci.h"
#include "gmac.h"

#define PORTS 2U
#define RING_COUNT 32U
#define GMAC2_ID 0x7a230014U
#define GMAC2_STATUS 0x7ffffU
#define GMAC2_FATAL ((1U << 13) | (1U << 12))

struct explorer_gmac
{
    struct eth_device ethernet;
    struct ls2k_gmac core;
    struct rt_mutex lock;
    const struct ls2k3000_pci_device *pci;
    rt_ubase_t base;
    uint8_t address[6];
    char name[4];
    unsigned int registered;
    unsigned int initialized;
    unsigned int fault;
    uint32_t dropped;
    uint32_t bus_errors;
};

static struct explorer_gmac ports[PORTS];

static uint32_t register_read(void *context, unsigned int reg)
{
    struct explorer_gmac *device = context;
    return *(volatile uint32_t *)(device->base + reg);
}

static void register_write(void *context, unsigned int reg, uint32_t value)
{
    struct explorer_gmac *device = context;
    *(volatile uint32_t *)(device->base + reg) = value;
    rt_hw_dmb();
}

static void delay_ms(void *context, unsigned int milliseconds)
{
    RT_UNUSED(context);
    rt_thread_mdelay(milliseconds);
}

static void barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static const struct ls2k_gmac_io io = {
    register_read, register_write, delay_ms, barrier,
};

static void service_thread(void *argument)
{
    struct explorer_gmac *device = argument;
    unsigned int poll = 0;

    for (;;)
    {
        rt_bool_t notify = RT_FALSE;
        int link_changed = 0;
        int link;
        uint32_t status;

        rt_thread_mdelay(2);
        rt_mutex_take(&device->lock, RT_WAITING_FOREVER);
        link = device->core.link;
        status = register_read(device, GMAC_DMA_STATUS);
        register_write(device, GMAC_DMA_STATUS, status & GMAC2_STATUS);

        if (!device->fault && (status & GMAC2_FATAL))
        {
            /* Stop bus mastering before reporting the interface down. The
             * DMA storage remains pinned even if the device stops responding. */
            device->bus_errors++;
            device->fault = 1;
            ls2k_gmac_stop(&device->core);
            ls2k3000_pci_command(device->pci, 0, 4);
            rt_kprintf("%s: fatal DMA status=%08x; controller isolated\n", device->name, status);
        }

        if (!device->fault)
        {
            ls2k_gmac_reclaim_tx(&device->core);

            if (++poll >= 100)
            {
                poll = 0;

                if (ls2k_gmac_poll_link(&device->core))
                {
                    device->core.link = 0;
                }
            }

            notify = !(device->core.rx.desc[device->core.rx_head].status & GMAC_DESC_OWN);
        }

        link_changed = link != device->core.link;
        link = device->core.link;
        rt_mutex_release(&device->lock);

        if (link_changed)
        {
            eth_device_linkchange(&device->ethernet, link ? RT_TRUE : RT_FALSE);
        }

        if (notify)
        {
            eth_device_ready(&device->ethernet);
        }
    }
}

static rt_err_t initialize(rt_device_t parent)
{
    struct explorer_gmac *device = (void *)parent;
    rt_thread_t worker;
    int result;

    if (device->initialized)
    {
        return RT_EOK;
    }

    result = ls2k3000_pci_command(device->pci, 6, 0);

    if (!result)
    {
        result = ls2k_gmac_start(&device->core, device->address, -1);
    }

    if (result)
    {
        ls2k_gmac_stop(&device->core);
        ls2k3000_pci_command(device->pci, 0, 4);
        rt_kprintf("%s: reset/PHY failed=%d\n", device->name, result);
        return -RT_ERROR;
    }

    worker = rt_thread_create(device->name, service_thread, device, 4096, 18, 10);

    if (!worker)
    {
        ls2k_gmac_stop(&device->core);
        ls2k3000_pci_command(device->pci, 0, 4);
        return -RT_ENOMEM;
    }

    device->initialized = 1;
    rt_thread_startup(worker);
    return RT_EOK;
}

static rt_err_t control(rt_device_t parent, int command, void *argument)
{
    struct explorer_gmac *device = (void *)parent;

    if (command != NIOCTL_GADDR)
    {
        return -RT_ENOSYS;
    }

    if (!argument)
    {
        return -RT_EINVAL;
    }

    rt_memcpy(argument, device->address, sizeof(device->address));
    return RT_EOK;
}

static rt_err_t transmit(rt_device_t parent, struct pbuf *packet)
{
    struct explorer_gmac *device = (void *)parent;
    rt_tick_t started = rt_tick_get();
    uint8_t *buffer;
    size_t bytes;
    int result;

    if (!packet || packet->tot_len <= ETH_PAD_SIZE)
    {
        return -RT_EINVAL;
    }

    bytes = packet->tot_len - ETH_PAD_SIZE;

    if (bytes > GMAC_FRAME_MAX)
    {
        return -RT_EINVAL;
    }

    for (;;)
    {
        rt_mutex_take(&device->lock, RT_WAITING_FOREVER);

        if (!device->core.link || device->fault)
        {
            rt_mutex_release(&device->lock);
            return -RT_ERROR;
        }

        buffer = ls2k_gmac_tx_buffer(&device->core);

        if (buffer)
        {
            break;
        }

        rt_mutex_release(&device->lock);

        if (rt_tick_get() - started >= rt_tick_from_millisecond(100))
        {
            return -RT_ETIMEOUT;
        }

        rt_thread_mdelay(1);
    }

    if (pbuf_copy_partial(packet, buffer, bytes, ETH_PAD_SIZE) != bytes)
    {
        result = -RT_ERROR;
    }
    else
    {
        result = ls2k_gmac_submit_tx(&device->core, bytes);
    }

    rt_mutex_release(&device->lock);
    return result ? -RT_ERROR : RT_EOK;
}

static struct pbuf *receive(rt_device_t parent)
{
    struct explorer_gmac *device = (void *)parent;
    struct pbuf *packet = RT_NULL;

    rt_mutex_take(&device->lock, RT_WAITING_FOREVER);

    for (unsigned int budget = 0; !device->fault && budget < RING_COUNT; ++budget)
    {
        const uint8_t *data;
        int bytes = ls2k_gmac_peek_rx(&device->core, &data);

        if (!bytes)
        {
            break;
        }

        if (bytes > 0)
        {
            packet = pbuf_alloc(PBUF_RAW, bytes + ETH_PAD_SIZE, PBUF_POOL);

#if ETH_PAD_SIZE
            if (packet)
            {
                pbuf_header(packet, -ETH_PAD_SIZE);
            }
#endif

            if (packet && pbuf_take(packet, data, bytes) != ERR_OK)
            {
                pbuf_free(packet);
                packet = RT_NULL;
            }

#if ETH_PAD_SIZE
            if (packet)
            {
                pbuf_header(packet, ETH_PAD_SIZE);
            }
#endif

            if (packet)
            {
                device->core.rx_packets++;
            }
            else
            {
                device->dropped++;
            }
        }

        ls2k_gmac_release_rx(&device->core);

        if (packet)
        {
            break;
        }
    }

    rt_mutex_release(&device->lock);
    return packet;
}

static const struct rt_device_ops device_ops = {
    .init = initialize,
    .control = control,
};

static int allocate_ring(struct ls2k_gmac_ring *ring)
{
    uint64_t descriptors;
    uint64_t buffers;

    ring->count = RING_COUNT;
    ring->desc = ls2k3000_dma_alloc(RING_COUNT * sizeof(*ring->desc), 64, &descriptors);
    ring->buffers = ls2k3000_dma_alloc(RING_COUNT * GMAC_BUFFER_SIZE, 64, &buffers);

    if (!ring->desc || !ring->buffers ||
        descriptors + RING_COUNT * sizeof(*ring->desc) > (UINT64_C(1) << 32) ||
        buffers + RING_COUNT * GMAC_BUFFER_SIZE > (UINT64_C(1) << 32))
    {
        return -RT_ENOMEM;
    }

    ring->desc_dma = descriptors;
    ring->buffers_dma = buffers;
    return RT_EOK;
}

static int register_port(unsigned int index)
{
    struct explorer_gmac *device = &ports[index];
    uint32_t low;
    uint32_t high;
    uint8_t combined = 0;
    int result;

    device->pci = ls2k3000_pci_find(0x020000, index);

    if (!device->pci || device->pci->id != GMAC2_ID)
    {
        return -RT_ENOSYS;
    }

    result = ls2k3000_pci_map(device->pci, 0, 0x1100, &device->base);

    if (result || ls2k3000_pci_command(device->pci, 2, 4))
    {
        return -RT_EIO;
    }

    low = register_read(device, GMAC_ADDR_LOW);
    high = register_read(device, GMAC_ADDR_HIGH);

    for (unsigned int byte = 0; byte < 6; ++byte)
    {
        device->address[byte] = byte < 4 ? low >> (byte * 8) : high >> ((byte - 4) * 8);
        combined |= device->address[byte];
    }

    if (!combined || (device->address[0] & 1))
    {
        rt_kprintf("GMAC%u: firmware did not provide a valid unicast MAC\n", index);
        return -RT_EINVAL;
    }

    rt_snprintf(device->name, sizeof(device->name), "e%u", index);
    device->core.io = &io;
    device->core.context = device;
    device->core.mdio_clock = BSP_LS2K3000_MDIO_CLOCK;
    device->core.reset_timeout_ms = 2000;
    device->core.status_w1c = GMAC2_STATUS;

    if (allocate_ring(&device->core.tx) || allocate_ring(&device->core.rx))
    {
        return -RT_ENOMEM;
    }

    rt_mutex_init(&device->lock, device->name, RT_IPC_FLAG_PRIO);
    device->ethernet.parent.ops = &device_ops;
    device->ethernet.eth_tx = transmit;
    device->ethernet.eth_rx = receive;
    result = eth_device_init_with_flag(&device->ethernet, device->name,
                                       NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
#if LWIP_IGMP
                                       | NETIF_FLAG_IGMP
#endif
                                       );

    if (result)
    {
        rt_mutex_detach(&device->lock);
    }
    else
    {
        device->registered = 1;
    }

    return result;
}

static int explorer_gmac_init(void)
{
    unsigned int initialized = 0;

    for (unsigned int index = 0; index < PORTS; ++index)
    {
        int result = register_port(index);

        if (!result)
        {
            initialized++;
        }
        else
        {
            rt_kprintf("GMAC%u: not registered (%d)\n", index, result);
        }
    }

    return initialized ? RT_EOK : -RT_ENOSYS;
}

INIT_DEVICE_EXPORT(explorer_gmac_init);

static long ls2k3000_net_status(void)
{
    for (unsigned int index = 0; index < PORTS; ++index)
    {
        struct explorer_gmac *device = &ports[index];

        if (!device->registered)
        {
            continue;
        }

        rt_mutex_take(&device->lock, RT_WAITING_FOREVER);
        rt_kprintf("%s phy=%u link=%d speed=%u tx=%llu rx=%llu drop=%u buserr=%u fault=%u\n",
                   device->name, device->core.phy, device->core.link, device->core.speed,
                   device->core.tx_packets, device->core.rx_packets,
                   device->dropped, device->bus_errors, device->fault);
        rt_mutex_release(&device->lock);
    }

    return RT_EOK;
}

MSH_CMD_EXPORT(ls2k3000_net_status, Show Explorer polled Ethernet state);
