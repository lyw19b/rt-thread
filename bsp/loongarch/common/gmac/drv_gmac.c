/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <netif/ethernetif.h>
#include <lwip/pbuf.h>
#include "ls2k.h"
#include "ls2k_gmac_platform.h"
#include "gmac.h"

#define DBG_TAG "ls2k.gmac"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

struct gmac_dma_memory
{
    struct ls2k_gmac_desc tx[BSP_GMAC_RING_SIZE];
    struct ls2k_gmac_desc rx[BSP_GMAC_RING_SIZE];
    rt_uint8_t tx_buffers[BSP_GMAC_RING_SIZE][GMAC_BUFFER_SIZE];
    rt_uint8_t rx_buffers[BSP_GMAC_RING_SIZE][GMAC_BUFFER_SIZE];
};

#ifdef BSP_GMAC_USING_ECHO_TEST
#define GMAC_TRACE_TAIL_SIZE 64U

struct gmac_tx_trace
{
    rt_uint32_t hash;
    rt_uint16_t length;
    rt_uint64_t sequence;
    rt_uint8_t tail[GMAC_TRACE_TAIL_SIZE];
};
#endif

struct gmac_device
{
    struct eth_device parent;
    struct ls2k_gmac mac;
    struct rt_mutex lock;
    struct rt_spinlock irq_lock;
    rt_ubase_t base;
    rt_uint8_t address[6];
    const char *name;
    int irq, phy_address;
    rt_bool_t fault, recovering;
    rt_bool_t initialized, registered;
    rt_uint32_t bus_errors, rx_dropped, recoveries;
#ifdef BSP_GMAC_USING_ECHO_TEST
    rt_uint32_t tx_checksum_errors;
    rt_uint32_t tx_reclaim_errors;
    rt_uint32_t tx_payload_errors;
    rt_uint32_t rx_checksum_errors;
    rt_uint32_t rx_payload_errors;
    rt_uint32_t rx_udp_packets;
    rt_uint32_t rx_udp_pbuf_drops;
    struct gmac_tx_trace tx_trace[BSP_GMAC_RING_SIZE];
#endif
};

/* This NOLOAD section is outside .bss and the heap. Only the uncached alias
 * is accessed, so DMA never depends on CPU cache snooping or cache flush stubs. */
#ifdef BSP_USING_GMAC0
static struct gmac_dma_memory dma0 __attribute__((section(".gmac_dma"), aligned(64)));
static struct gmac_device gmac0;
#endif
#ifdef BSP_USING_GMAC1
static struct gmac_dma_memory dma1 __attribute__((section(".gmac_dma"), aligned(64)));
static struct gmac_device gmac1;
#endif

static uint32_t _reg_read(void *context, unsigned int reg)
{
    struct gmac_device *dev = context;
    return *(volatile rt_uint32_t *)(dev->base + reg);
}

static void _reg_write(void *context, unsigned int reg, uint32_t value)
{
    struct gmac_device *dev = context;
    *(volatile rt_uint32_t *)(dev->base + reg) = value;
    __asm__ volatile("dbar 0" ::: "memory");
}

static void _delay_ms(void *context, unsigned int ms)
{
    RT_UNUSED(context);
    rt_thread_mdelay(ms);
}

static void _dma_barrier(void *context)
{
    RT_UNUSED(context);
    __asm__ volatile("dbar 0" ::: "memory");
}

static const struct ls2k_gmac_io gmac_io =
{
    _reg_read, _reg_write, _delay_ms, _dma_barrier
};

static void _irq(int vector, void *parameter)
{
    struct gmac_device *dev = parameter;
    rt_base_t level = rt_spin_lock_irqsave(&dev->irq_lock);
    rt_uint32_t status;
    rt_bool_t notify = RT_FALSE;

    RT_UNUSED(vector);
    if (!dev->recovering)
    {
        status = _reg_read(dev, GMAC_DMA_STATUS);
        _reg_write(dev, GMAC_DMA_STATUS, status & GMAC_DMA_STATUS_W1C);
        if (status & GMAC_DMA_FATAL)
        {
            _reg_write(dev, GMAC_DMA_INT_ENABLE, 0);
            __atomic_fetch_add(&dev->bus_errors, 1, __ATOMIC_RELAXED);
            __atomic_store_n(&dev->fault, RT_TRUE, __ATOMIC_RELEASE);
        }
        else if (status & (GMAC_DMA_RX_DONE | GMAC_DMA_RX_UNAVAILABLE))
        {
            rt_uint32_t mask = _reg_read(dev, GMAC_DMA_INT_ENABLE);
            _reg_write(dev, GMAC_DMA_INT_ENABLE,
                       mask & ~(GMAC_DMA_RX_DONE | GMAC_DMA_RX_UNAVAILABLE));
            notify = RT_TRUE;
        }
    }
    rt_spin_unlock_irqrestore(&dev->irq_lock, level);
    /* Never enter lwIP while holding the register lock. */
    if (notify)
        eth_device_ready(&dev->parent);
}

static void _service(void *parameter)
{
    struct gmac_device *dev = parameter;

    for (;;)
    {
        int old_link, error;
        unsigned int old_speed;
        int old_duplex;

        rt_thread_mdelay(200);
        rt_mutex_take(&dev->lock, RT_WAITING_FOREVER);
        old_link = dev->mac.link;
        old_speed = dev->mac.speed;
        old_duplex = dev->mac.duplex;
        if (__atomic_load_n(&dev->fault, __ATOMIC_ACQUIRE))
        {
            rt_base_t level = rt_spin_lock_irqsave(&dev->irq_lock);
            dev->recovering = RT_TRUE;
            _reg_write(dev, GMAC_DMA_INT_ENABLE, 0);
            rt_spin_unlock_irqrestore(&dev->irq_lock, level);
            rt_hw_interrupt_mask(dev->irq);
            if (dev->mac.running)
                dev->mac.tx_errors += dev->mac.tx_pending;
            error = ls2k_gmac_start(&dev->mac, dev->address, dev->phy_address);
            level = rt_spin_lock_irqsave(&dev->irq_lock);
            if (!error)
            {
                __atomic_store_n(&dev->fault, RT_FALSE, __ATOMIC_RELEASE);
                dev->recoveries++;
                _reg_write(dev, GMAC_DMA_INT_ENABLE, GMAC_DMA_IRQ_MASK);
            }
            dev->recovering = RT_FALSE;
            rt_spin_unlock_irqrestore(&dev->irq_lock, level);
            if (!error)
                rt_hw_interrupt_umask(dev->irq);
        }
        if (!__atomic_load_n(&dev->fault, __ATOMIC_ACQUIRE))
        {
            ls2k_gmac_poll_link(&dev->mac);
            ls2k_gmac_reclaim_tx(&dev->mac);
            /* Covers RX ring exhaustion and lost mailbox notifications. */
            if (!(dev->mac.rx.desc[dev->mac.rx_head].status & GMAC_DESC_OWN))
                eth_device_ready(&dev->parent);
        }
        if (old_link != dev->mac.link)
            eth_device_linkchange(&dev->parent, dev->mac.link);
        {
            rt_base_t level = rt_spin_lock_irqsave(&dev->parent.spinlock);
            rt_bool_t pending = dev->parent.link_changed;
            rt_spin_unlock_irqrestore(&dev->parent.spinlock, level);
            /* A link event also needs retry if the shared RX mailbox was full. */
            if (pending)
                eth_device_ready(&dev->parent);
        }
        if (dev->mac.link && (!old_link || old_speed != dev->mac.speed || old_duplex != dev->mac.duplex))
            LOG_I("%s: %u Mbps %s duplex", dev->name, dev->mac.speed,
                  dev->mac.duplex ? "full" : "half");
        else if (old_link && !dev->mac.link)
            LOG_I("%s: link down", dev->name);
        rt_mutex_release(&dev->lock);
    }
}

static rt_err_t _device_init(rt_device_t device)
{
    struct gmac_device *dev = (struct gmac_device *)device;
    rt_thread_t service;
    int error;

    if (dev->initialized)
        return RT_EOK;
    error = ls2k_gmac_start(&dev->mac, dev->address, dev->phy_address);
    if (error)
    {
        LOG_E("%s: reset/PHY initialization failed (%d)", dev->name, error);
        return -RT_ERROR;
    }
    service = rt_thread_create(dev->name, _service, dev, 4096, RT_THREAD_PRIORITY_MAX - 4, 10);
    if (!service)
    {
        ls2k_gmac_stop(&dev->mac);
        return -RT_ENOMEM;
    }
    rt_hw_interrupt_install(dev->irq, _irq, dev, dev->name);
    dev->initialized = RT_TRUE;
    _reg_write(dev, GMAC_DMA_INT_ENABLE, GMAC_DMA_IRQ_MASK);
    rt_hw_interrupt_umask(dev->irq);
    rt_thread_startup(service);
    return RT_EOK;
}

static rt_err_t _control(rt_device_t device, int command, void *argument)
{
    struct gmac_device *dev = (struct gmac_device *)device;

    if (command != NIOCTL_GADDR)
        return -RT_ENOSYS;
    if (!argument)
        return -RT_EINVAL;
    rt_memcpy(argument, dev->address, sizeof(dev->address));
    return RT_EOK;
}

#ifdef BSP_GMAC_USING_ECHO_TEST
static rt_uint32_t _tx_hash(const rt_uint8_t *frame, rt_size_t length)
{
    const volatile rt_uint8_t *data = frame;
    rt_uint32_t hash = 2166136261U;

    for (rt_size_t i = 0; i < length; i++)
    {
        hash = (hash ^ data[i]) * 16777619U;
    }

    return hash;
}

static void _trace_tx_submit(struct gmac_device *dev, const rt_uint8_t *frame,
                             rt_size_t length)
{
    struct gmac_tx_trace *trace = &dev->tx_trace[dev->mac.tx_head];
    rt_size_t tail_length = length < GMAC_TRACE_TAIL_SIZE ? length : GMAC_TRACE_TAIL_SIZE;
    rt_size_t header;

    /* Drain the copy before sampling its uncached DMA alias. */
    _dma_barrier(dev);
    trace->length = length;
    trace->hash = _tx_hash(frame, length);
    trace->sequence = UINT64_MAX;
    rt_memcpy(trace->tail, frame + length - tail_length, tail_length);

    if (length < 50 || frame[12] != 8 || frame[13] != 0 || frame[23] != 17)
    {
        return;
    }

    header = (frame[14] & 15) * 4;

    if (header < 20 || length < 14 + header + 16 ||
        frame[14 + header] != 0x23 || frame[15 + header] != 0x28)
    {
        return;
    }

    trace->sequence = 0;

    for (unsigned int i = 0; i < 8; i++)
    {
        trace->sequence = (trace->sequence << 8) | frame[22 + header + i];
    }
}

static void _trace_tail(const char *label, const rt_uint8_t *bytes, rt_size_t length)
{
    rt_kprintf("%s", label);

    for (rt_size_t i = 0; i < length; i++)
    {
        rt_kprintf(" %02x", bytes[i]);
    }

    rt_kprintf("\n");
}

static void _trace_tx_complete(struct ls2k_gmac *mac, unsigned int slot, size_t length)
{
    struct gmac_device *dev = mac->context;
    struct gmac_tx_trace *trace = &dev->tx_trace[slot];
    const rt_uint8_t *frame = mac->tx.buffers + slot * GMAC_BUFFER_SIZE;
    rt_size_t original_length = trace->length;
    rt_uint32_t hash;

    if (!original_length || original_length > GMAC_FRAME_MAX)
    {
        return;
    }

    hash = _tx_hash(frame, original_length);

    if (hash != trace->hash || length != original_length)
    {
        dev->tx_reclaim_errors++;

        if (dev->tx_reclaim_errors <= 3)
        {
            rt_size_t tail_length = original_length < GMAC_TRACE_TAIL_SIZE ?
                                    original_length : GMAC_TRACE_TAIL_SIZE;

            rt_kprintf("%s: TX changed after submit slot=%u dma=%08x seq=%llu "
                       "len=%u/%u hash=%08x/%08x status=%08x\n",
                       dev->name, slot, mac->tx.buffers_dma + slot * GMAC_BUFFER_SIZE,
                       trace->sequence, (unsigned int)original_length,
                       (unsigned int)length, trace->hash, hash, mac->tx.desc[slot].status);
            _trace_tail("TX tail before:", trace->tail, tail_length);
            _trace_tail("TX tail after :", frame + original_length - tail_length, tail_length);
        }
    }

    trace->length = 0;
}

/* Inspect DMA bytes without changing delivery. The echo pattern check is
 * essential: a changed 64-byte line can preserve the 16-bit UDP checksum. */
static rt_bool_t _check_udp_frame(struct gmac_device *dev, const rt_uint8_t *frame,
                                  rt_size_t bytes, rt_bool_t transmit)
{
    const rt_uint8_t *ip, *udp;
    unsigned int header, length, sum = 0;
    rt_bool_t echo;

    if (bytes < 42 || frame[12] != 8 || frame[13] != 0)
    {
        return RT_FALSE;
    }

    ip = frame + 14;
    header = (ip[0] & 15) * 4;

    if ((ip[0] >> 4) != 4 || header < 20 || bytes < 14 + header + 8 ||
        ip[9] != 17 || (ip[6] & 0x3f) || ip[7])
    {
        return RT_FALSE;
    }

    udp = ip + header;
    length = ((unsigned int)udp[4] << 8) | udp[5];

    if (length < 8 || length > bytes - 14 - header)
    {
        return RT_FALSE;
    }

    echo = length >= 16 && udp[transmit ? 0 : 2] == 0x23 &&
           udp[transmit ? 1 : 3] == 0x28;

    if (echo && !transmit)
    {
        dev->rx_udp_packets++;
    }

    if (udp[6] | udp[7])
    {
        for (unsigned int i = 12; i < 20; i += 2)
        {
            sum += ((unsigned int)ip[i] << 8) | ip[i + 1];
        }

        sum += 17 + length;

        for (unsigned int i = 0; i < length; i += 2)
        {
            sum += (unsigned int)udp[i] << 8;

            if (i + 1 < length)
            {
                sum += udp[i + 1];
            }
        }

        while (sum >> 16)
        {
            sum = (sum & 0xffff) + (sum >> 16);
        }

        if (sum != 0xffff)
        {
            rt_uint32_t *errors = transmit ? &dev->tx_checksum_errors :
                                            &dev->rx_checksum_errors;

            (*errors)++;

            if (*errors <= 3)
            {
                rt_kprintf("%s: %s UDP checksum invalid at DMA len=%u sum=%04x "
                           "header=%02x%02x\n", dev->name, transmit ? "TX" : "RX",
                           length, sum, udp[6], udp[7]);
            }
        }
    }

    if (echo)
    {
        rt_uint64_t sequence = 0;
        unsigned int expected_length, first = 0;
        rt_uint8_t expected = 0;
        rt_bool_t mismatch;

        for (unsigned int i = 0; i < 8; i++)
        {
            sequence = (sequence << 8) | udp[8 + i];
        }

        expected_length = 24 + sequence % 1457;
        mismatch = length != expected_length;

        for (unsigned int i = 16; !mismatch && i < length; i++)
        {
            expected = (rt_uint8_t)(sequence + (i - 16) * 17);

            if (udp[i] != expected)
            {
                first = i;
                mismatch = RT_TRUE;
            }
        }

        if (mismatch)
        {
            rt_uint32_t *errors = transmit ? &dev->tx_payload_errors :
                                            &dev->rx_payload_errors;
            unsigned int slot = transmit ? dev->mac.tx_head : dev->mac.rx_head;
            rt_uint32_t base = transmit ? dev->mac.tx.buffers_dma : dev->mac.rx.buffers_dma;

            (*errors)++;

            if (*errors <= 3)
            {
                rt_size_t tail_length = bytes < GMAC_TRACE_TAIL_SIZE ? bytes : GMAC_TRACE_TAIL_SIZE;

                rt_kprintf("%s: %s UDP pattern invalid slot=%u dma=%08x seq=%llu "
                           "len=%u expected_len=%u first_frame_byte=%u actual=%02x "
                           "expected=%02x\n", dev->name, transmit ? "TX" : "RX", slot,
                           base + slot * GMAC_BUFFER_SIZE, sequence, length,
                           expected_length, first ? 14 + header + first : 0,
                           first ? udp[first] : 0, expected);
                _trace_tail("DMA frame tail:", frame + bytes - tail_length, tail_length);
            }
        }
    }

    return echo;
}
#endif

static rt_err_t _transmit(rt_device_t device, struct pbuf *packet)
{
    struct gmac_device *dev = (struct gmac_device *)device;
    rt_tick_t start = rt_tick_get();
    rt_uint8_t *buffer;
    int error;
    rt_size_t length;

    if (!packet || packet->tot_len <= ETH_PAD_SIZE)
        return -RT_EINVAL;
    length = packet->tot_len - ETH_PAD_SIZE;
    if (length > GMAC_FRAME_MAX)
        return -RT_EINVAL;
    for (;;)
    {
        rt_mutex_take(&dev->lock, RT_WAITING_FOREVER);
        if (!dev->mac.link || __atomic_load_n(&dev->fault, __ATOMIC_ACQUIRE))
        {
            rt_mutex_release(&dev->lock);
            return -RT_ERROR;
        }
        buffer = ls2k_gmac_tx_buffer(&dev->mac);
        if (buffer)
            break;
        rt_mutex_release(&dev->lock);
        if (rt_tick_get() - start >= rt_tick_from_millisecond(100))
            return -RT_ETIMEOUT;
        rt_thread_mdelay(1);
    }
    if (pbuf_copy_partial(packet, buffer, length, ETH_PAD_SIZE) != length)
        error = -RT_ERROR;
    else
    {
#ifdef BSP_GMAC_USING_ECHO_TEST
        _check_udp_frame(dev, buffer, length, RT_TRUE);
        _trace_tx_submit(dev, buffer, length);
#endif
        error = ls2k_gmac_submit_tx(&dev->mac, length);
    }
    rt_mutex_release(&dev->lock);
    return error ? -RT_ERROR : RT_EOK;
}

static struct pbuf *_receive(rt_device_t device)
{
    struct gmac_device *dev = (struct gmac_device *)device;
    struct pbuf *packet = RT_NULL;
    unsigned int budget = BSP_GMAC_RING_SIZE;

    rt_mutex_take(&dev->lock, RT_WAITING_FOREVER);
    if (__atomic_load_n(&dev->fault, __ATOMIC_ACQUIRE) || !dev->mac.running)
        goto out;
    while (budget--)
    {
        const uint8_t *data;
        int length = ls2k_gmac_peek_rx(&dev->mac, &data);
        if (!length)
            break;
        if (length > 0)
        {
#ifdef BSP_GMAC_USING_ECHO_TEST
            rt_bool_t echo = _check_udp_frame(dev, data, length, RT_FALSE);
#endif
            packet = pbuf_alloc(PBUF_RAW, length + ETH_PAD_SIZE, PBUF_POOL);
#if ETH_PAD_SIZE
            if (packet) pbuf_header(packet, -ETH_PAD_SIZE);
#endif
            if (packet && pbuf_take(packet, data, length) != ERR_OK)
            {
                pbuf_free(packet);
                packet = RT_NULL;
            }
#if ETH_PAD_SIZE
            if (packet) pbuf_header(packet, ETH_PAD_SIZE);
#endif
            if (packet)
                dev->mac.rx_packets++;
            else
            {
                dev->rx_dropped++;
#ifdef BSP_GMAC_USING_ECHO_TEST
                if (echo)
                {
                    dev->rx_udp_pbuf_drops++;
                }
#endif
            }
        }
        ls2k_gmac_release_rx(&dev->mac);
        if (packet)
            goto out;
    }
    /* Re-enable before the final ownership check: a frame arriving during
     * drain must either trigger an IRQ or be explicitly queued below. */
    {
        rt_base_t level = rt_spin_lock_irqsave(&dev->irq_lock);
        if (!__atomic_load_n(&dev->fault, __ATOMIC_ACQUIRE))
            _reg_write(dev, GMAC_DMA_INT_ENABLE, GMAC_DMA_IRQ_MASK);
        rt_spin_unlock_irqrestore(&dev->irq_lock, level);
        if (!(dev->mac.rx.desc[dev->mac.rx_head].status & GMAC_DESC_OWN))
            eth_device_ready(&dev->parent);
    }
out:
    rt_mutex_release(&dev->lock);
    return packet;
}

static const struct rt_device_ops device_ops =
{
    .init = _device_init,
    .control = _control,
};

static int _hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static rt_bool_t _valid_address(const rt_uint8_t address[6])
{
    unsigned int i;
    rt_uint8_t combined = 0;

    for (i = 0; i < 6; i++) combined |= address[i];
    return combined && !(address[0] & 1);
}

static int _address_init(struct gmac_device *dev, const char *setting, unsigned int port)
{
    unsigned int i;
    rt_uint32_t low, high;

    if (setting[0])
    {
        if (rt_strlen(setting) != 17)
            return -RT_EINVAL;
        for (i = 0; i < 6; i++)
        {
            int high_digit = _hex_digit(setting[i * 3]);
            int low_digit = _hex_digit(setting[i * 3 + 1]);
            if (high_digit < 0 || low_digit < 0 || (i < 5 && setting[i * 3 + 2] != ':'))
                return -RT_EINVAL;
            dev->address[i] = (high_digit << 4) | low_digit;
        }
        return _valid_address(dev->address) ? RT_EOK : -RT_EINVAL;
    }
    low = _reg_read(dev, GMAC_ADDR_LOW);
    high = _reg_read(dev, GMAC_ADDR_HIGH);
    for (i = 0; i < 4; i++) dev->address[i] = low >> (i * 8);
    dev->address[4] = high;
    dev->address[5] = high >> 8;
    if (!_valid_address(dev->address))
    {
        const rt_uint8_t fallback[6] = {0x02, 0x00, 0x00, 0x00, 0x10, 0x00};
        rt_memcpy(dev->address, fallback, sizeof(fallback));
        dev->address[5] = port;
        LOG_W("%s: using fallback MAC; configure a unique address before deployment", dev->name);
    }
    return RT_EOK;
}

static int _register_port(struct gmac_device *dev, struct gmac_dma_memory *storage,
                          unsigned int port, int phy, const char *address)
{
    struct gmac_dma_memory *dma = (void *)LS2K_UNCACHED(storage);
    int error;

    dev->name = port ? "e1" : "e0";
    dev->phy_address = phy;
    error = ls2k_gmac_platform_init(port, &dev->base, &dev->irq);
    if (error)
    {
        LOG_E("%s: platform initialization failed (%d)", dev->name, error);
        return error;
    }
    if (LS2K_PHYSICAL(storage) + sizeof(*storage) > ls2k_gmac_dma_limit())
        return -RT_EINVAL;
    error = _address_init(dev, address, port);
    if (error)
        return error;
    rt_memset(dma, 0, sizeof(*dma));
    dev->mac.io = &gmac_io;
    dev->mac.context = dev;
    dev->mac.mdio_clock = BSP_GMAC_MDIO_CLOCK;
#ifdef BSP_GMAC_USING_ECHO_TEST
    dev->mac.tx_complete = _trace_tx_complete;
#endif
    dev->mac.tx = (struct ls2k_gmac_ring){dma->tx, &dma->tx_buffers[0][0],
        LS2K_PHYSICAL(dma->tx), LS2K_PHYSICAL(dma->tx_buffers), BSP_GMAC_RING_SIZE};
    dev->mac.rx = (struct ls2k_gmac_ring){dma->rx, &dma->rx_buffers[0][0],
        LS2K_PHYSICAL(dma->rx), LS2K_PHYSICAL(dma->rx_buffers), BSP_GMAC_RING_SIZE};
    rt_mutex_init(&dev->lock, dev->name, RT_IPC_FLAG_PRIO);
    rt_spin_lock_init(&dev->irq_lock);
    dev->parent.parent.ops = &device_ops;
    dev->parent.eth_rx = _receive;
    dev->parent.eth_tx = _transmit;
    /* Start link-down. The service thread publishes the measured PHY state;
     * ETHIF_LINK_PHYUP would assert a link even on an unplugged port. */
    error = eth_device_init_with_flag(&dev->parent, dev->name,
             NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
#if LWIP_IGMP
             | NETIF_FLAG_IGMP
#endif
             );
    if (error)
        rt_mutex_detach(&dev->lock);
    else
        dev->registered = RT_TRUE;
    return error;
}

static int rt_hw_gmac_init(void)
{
    int result = RT_EOK;
    int error;

#ifdef BSP_USING_GMAC0
    error = _register_port(&gmac0, &dma0, 0, BSP_GMAC0_PHY, BSP_GMAC0_MAC);
    if (error) result = error;
#endif
#ifdef BSP_USING_GMAC1
    error = _register_port(&gmac1, &dma1, 1, BSP_GMAC1_PHY, BSP_GMAC1_MAC);
    if (error) result = error;
#endif
    return result;
}
INIT_DEVICE_EXPORT(rt_hw_gmac_init);

#ifdef RT_USING_FINSH
static void _show_status(struct gmac_device *dev)
{
    if (!dev->registered)
        return;
    rt_mutex_take(&dev->lock, RT_WAITING_FOREVER);
    rt_kprintf("%s phy=%u link=%d speed=%u duplex=%d tx=%llu rx=%llu "
               "txerr=%llu rxerr=%llu drop=%u buserr=%u recover=%u\n",
               dev->name, dev->mac.phy, dev->mac.link, dev->mac.speed, dev->mac.duplex,
               dev->mac.tx_packets, dev->mac.rx_packets, dev->mac.tx_errors,
               dev->mac.rx_errors, dev->rx_dropped, __atomic_load_n(&dev->bus_errors, __ATOMIC_RELAXED), dev->recoveries);
#ifdef BSP_GMAC_USING_ECHO_TEST
    rt_kprintf("%s tx_checksum_errors=%u tx_reclaim_errors=%u\n",
               dev->name, dev->tx_checksum_errors, dev->tx_reclaim_errors);
    rt_kprintf("%s tx_payload_errors=%u rx_udp_packets=%u rx_checksum_errors=%u "
               "rx_payload_errors=%u rx_udp_pbuf_drops=%u\n", dev->name,
               dev->tx_payload_errors, dev->rx_udp_packets, dev->rx_checksum_errors,
               dev->rx_payload_errors, dev->rx_udp_pbuf_drops);
#endif
    rt_mutex_release(&dev->lock);
}

static void gmac_status(void)
{
#ifdef BSP_USING_GMAC0
    _show_status(&gmac0);
#endif
#ifdef BSP_USING_GMAC1
    _show_status(&gmac1);
#endif
}
MSH_CMD_EXPORT(gmac_status, Show LS2K GMAC link and DMA counters);
#endif
