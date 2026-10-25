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
 *   LS2K0300 EHCI adapter for CherryUSB with high-speed hub/MSC transport.
 * Notes:
 *   Firmware supplies VBUS and PHY reference-clock configuration. OHCI and
 *   OTG are not initialized. A mutex serializes all schedule modifications;
 *   the IRQ only wakes the poll thread. No caller buffer is exposed to DMA.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"
#include "usbh_core.h"
#include "usbh_hub.h"
#include "ls2k_ehci.h"

#define USB_IRQ 46
#define USB_BASE 0x16080000UL
#define USB_ASYNC_SLOTS 4

static struct ls2k_usb_dma usb_storage
    __attribute__((section(".usb_dma"), aligned(4096)));
static struct ls2k_ehci controller;
static struct rt_mutex host_lock;
static struct rt_semaphore host_event;
static struct usbh_bus *host_bus;
static struct usbh_urb *pending[USB_ASYNC_SLOTS];
static rt_ubase_t operational;
static bool reset_change, port_power_control;
static rt_uint32_t requests, errors;
static uint32_t next_poll[USB_ASYNC_SLOTS];
static bool last_connected, connection_change, root_notified;

static uint32_t usb_read(void *ctx, unsigned int offset)
{
    RT_UNUSED(ctx);
    return HWREG32(operational + offset);
}

static void usb_write(void *ctx, unsigned int offset, uint32_t value)
{
    RT_UNUSED(ctx);
    HWREG32(operational + offset) = value;
}

static uint32_t usb_time(void *ctx)
{
    RT_UNUSED(ctx);
    return (uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
}

static void usb_wait(void *ctx, unsigned int ms)
{
    RT_UNUSED(ctx);
    rt_thread_mdelay(ms);
}

static void usb_barrier(void *ctx)
{
    RT_UNUSED(ctx);
    rt_hw_dmb();
}

static bool usb_cancelled(void *ctx)
{
    RT_UNUSED(ctx);
    return false;
}

static const struct ls2k_usb_io usb_io =
{
    usb_read, usb_write, usb_time, usb_wait, usb_barrier, usb_cancelled
};

static void port_update(uint32_t clear, uint32_t set, uint32_t acknowledge)
{
    uint32_t value = usb_read(NULL, LUSB_PORT) & ~LUSB_PORT_W1C;

    usb_write(NULL, LUSB_PORT, (value & ~clear) | set | acknowledge);
    rt_hw_dmb();
}

static int execute_urb(struct usbh_urb *urb, uint32_t timeout)
{
    struct ls2k_usb_transfer x = {0};
    unsigned int type = USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes);
    int ret;

    x.address = urb->hport->dev_addr;
    x.endpoint = urb->ep->bEndpointAddress & 15;
    x.input = (urb->ep->bEndpointAddress & 0x80) != 0;
    x.max_packet = USB_GET_MAXPACKETSIZE(urb->ep->wMaxPacketSize);
    x.interrupt = type == USB_ENDPOINT_TYPE_INTERRUPT;
    if (x.interrupt)
    {
        x.interval_uframes = 1U << (urb->ep->bInterval - 1);
    }

    x.toggle = urb->data_toggle;
    x.buffer = urb->transfer_buffer;
    x.length = urb->transfer_buffer_length;
    x.timeout_ms = timeout;
    if (type == USB_ENDPOINT_TYPE_CONTROL)
    {
        if (!urb->setup || urb->setup->wLength != x.length)
        {
            return -USB_ERR_INVAL;
        }

        x.setup = (const uint8_t *)urb->setup;
        x.input = (urb->setup->bmRequestType & 0x80) != 0;
    }

    urb->errorcode = -USB_ERR_BUSY;
    urb->hcpriv = &controller;
    ret = ls2k_ehci_transfer(&controller, &x);
    urb->actual_length = x.actual;
    urb->data_toggle = x.toggle;
    urb->errorcode = ret;
    urb->hcpriv = NULL;
    ++requests;
    if (ret && !(x.interrupt && ret == -USB_ERR_TIMEOUT))
    {
        ++errors;
    }

    return ret;
}

static void usb_interrupt(int vector, void *parameter)
{
    RT_UNUSED(vector);
    RT_UNUSED(parameter);
    /* Mask until the worker acknowledges PCD. No descriptor access in ISR. */
    rt_hw_interrupt_mask(USB_IRQ);
    rt_sem_release(&host_event);
}

static void usb_worker(void *parameter)
{
    RT_UNUSED(parameter);

    for (;;)
    {
        rt_sem_take(&host_event, rt_tick_from_millisecond(10));
        rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
        if (controller.ready)
        {
            uint32_t port = usb_read(NULL, LUSB_PORT);

            if ((port & 1) != last_connected)
            {
                connection_change = true;
            }

            if (!root_notified && ((port & LUSB_PORT_W1C) || connection_change))
            {
                size_t flags = usb_osal_enter_critical_section();

                host_bus->hcd.roothub.int_buffer[0] = 2;
                usb_osal_leave_critical_section(flags);
                /* Keep at most one root change outstanding. Flooding the hub
                 * queue during debounce could drop an external hub event. */
                root_notified = usb_osal_mq_send(host_bus->hub_mq,
                                  (uintptr_t)&host_bus->hcd.roothub) == 0;
            }

            last_connected = (port & 1) != 0;

            usb_write(NULL, LUSB_STS, 4); /* PCD only; never hide host errors. */
            for (unsigned int i = 0; i < USB_ASYNC_SLOTS; ++i)
            {
                struct usbh_urb *urb = pending[i];
                int ret;

                if (!urb || (int32_t)(usb_time(NULL) - next_poll[i]) < 0)
                {
                    continue;
                }

                ret = execute_urb(urb, 4);
                if ((ret == -USB_ERR_TIMEOUT || (!ret && !urb->actual_length)) &&
                    controller.ready)
                {
                    urb->hcpriv = &pending[i];
                    next_poll[i] = usb_time(NULL) +
                                   ((1U << (urb->ep->bInterval - 1)) + 7) / 8;
                    continue;
                }

                pending[i] = NULL;
                if (urb->complete)
                {
                    urb->complete(urb->arg, ret ? ret : (int)urb->actual_length);
                }
            }

            if (controller.ready)
            {
                rt_hw_interrupt_umask(USB_IRQ);
            }
        }

        rt_mutex_release(&host_lock);
    }
}

int usb_hc_init(struct usbh_bus *bus)
{
    int ret = -USB_ERR_NODEV;

    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    if (bus == host_bus && !controller.quarantined)
    {
        ret = ls2k_ehci_init(&controller, LOONGARCH_ALIAS_PHYS(&usb_storage));
        if (!ret)
        {
            bus->hcd.roothub.speed = USB_SPEED_HIGH;
            port_update(0, 1U << 12, 0);
            usb_write(NULL, LUSB_INTR, 4);
            rt_hw_interrupt_umask(USB_IRQ);
            rt_sem_release(&host_event);
        }
    }

    rt_mutex_release(&host_lock);
    rt_kprintf("USB EHCI init=%d, DMA=%p, quarantine=%d\n",
               ret, (void *)(rt_ubase_t)controller.dma_address, controller.quarantined);
    return ret;
}

int usb_hc_deinit(struct usbh_bus *bus)
{
    int ret;

    RT_UNUSED(bus);
    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    rt_hw_interrupt_mask(USB_IRQ);
    ret = ls2k_ehci_stop(&controller);
    rt_mutex_release(&host_lock);
    return ret;
}

uint16_t usbh_get_frame_number(struct usbh_bus *bus)
{
    RT_UNUSED(bus);
    return (usb_read(NULL, LUSB_FRAME) >> 3) & 0x7ff;
}

int usbh_roothub_control(struct usbh_bus *bus, struct usb_setup_packet *setup, uint8_t *buf)
{
    uint32_t port, status = 0;
    int ret = 0;

    if (bus != host_bus || !setup || (setup->bmRequestType & 0x1f) != 3 ||
        setup->wIndex != 1)
    {
        return -USB_ERR_INVAL;
    }

    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    if (!controller.ready)
    {
        ret = -USB_ERR_NODEV;
        goto out;
    }

    port = usb_read(NULL, LUSB_PORT);
    switch (setup->bRequest)
    {
    case HUB_REQUEST_GET_STATUS:
        if (!buf || setup->wLength < 4)
        {
            ret = -USB_ERR_INVAL;
            break;
        }

        status = (port & 1) | ((port & 4) ? 2 : 0) |
                 ((port & 0x10) ? (1U << 3) : 0) |
                 ((port & 0x1000) || !port_power_control ? (1U << 8) : 0) |
                 ((port & 4) ? (1U << 10) : 0) |
                 ((port & 2) || connection_change ? (1U << 16) : 0) |
                 ((port & 8) ? (1U << 17) : 0) |
                 ((port & 0x20) ? (1U << 19) : 0) |
                 (reset_change ? (1U << 20) : 0);
        memcpy(buf, &status, 4);
        break;

    case HUB_REQUEST_SET_FEATURE:
        if (setup->wValue == HUB_PORT_FEATURE_POWER)
        {
            port_update(0, 1U << 12, 0);
        }
        else if (setup->wValue == HUB_PORT_FEATURE_RESET)
        {
            uint32_t start;

            port_update(4, 1U << 8, 0);
            rt_thread_mdelay(50);
            port_update(1U << 8, 0, 0);
            start = usb_time(NULL);
            while (usb_read(NULL, LUSB_PORT) & (1U << 8))
            {
                if ((uint32_t)(usb_time(NULL) - start) >= 100)
                {
                    ret = -USB_ERR_TIMEOUT;
                    break;
                }

                rt_thread_mdelay(1);
            }

            rt_thread_mdelay(10);
            reset_change = true;
            if (!ret && !(usb_read(NULL, LUSB_PORT) & 4))
            {
                rt_kprintf("USB: device did not enable at high speed; OHCI is not enabled\n");
                ret = -USB_ERR_NOTSUPP;
            }
        }
        else
        {
            ret = -USB_ERR_NOTSUPP;
        }
        break;

    case HUB_REQUEST_CLEAR_FEATURE:
        root_notified = false;
        switch (setup->wValue)
        {
        case HUB_PORT_FEATURE_C_CONNECTION:
            connection_change = false;
            port_update(0, 0, 2);
            break;
        case HUB_PORT_FEATURE_C_ENABLE:
            port_update(0, 0, 8);
            break;
        case HUB_PORT_FEATURE_C_OVER_CURREN:
            port_update(0, 0, 0x20);
            break;
        case HUB_PORT_FEATURE_C_RESET:
            reset_change = false;
            break;
        case HUB_PORT_FEATURE_ENABLE:
            port_update(4, 0, 0);
            break;
        case HUB_PORT_FEATURE_POWER:
            port_update(1U << 12, 0, 0);
            break;
        default:
            ret = -USB_ERR_NOTSUPP;
            break;
        }
        break;

    default:
        ret = -USB_ERR_NOTSUPP;
        break;
    }

out:
    rt_mutex_release(&host_lock);
    return ret;
}

int usbh_submit_urb(struct usbh_urb *urb)
{
    unsigned int type;
    int ret;

    if (!urb || !urb->hport || !urb->ep || urb->hport->bus != host_bus)
    {
        return -USB_ERR_INVAL;
    }

    type = USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes);
    if (urb->hport->speed != USB_SPEED_HIGH ||
        type == USB_ENDPOINT_TYPE_ISOCHRONOUS ||
        (urb->ep->wMaxPacketSize & 0x1800) ||
        (type == USB_ENDPOINT_TYPE_INTERRUPT &&
         (!urb->ep->bInterval || urb->ep->bInterval > 14)) ||
        (!urb->timeout && type != USB_ENDPOINT_TYPE_INTERRUPT))
    {
        return -USB_ERR_NOTSUPP;
    }

    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    if (!urb->hport->connected || !controller.ready)
    {
        ret = -USB_ERR_NOTCONN;
    }
    else if (urb->hcpriv)
    {
        ret = -USB_ERR_BUSY;
    }
    else if (!urb->timeout)
    {
        ret = -USB_ERR_NOMEM;
        for (unsigned int i = 0; i < USB_ASYNC_SLOTS; ++i)
        {
            if (!pending[i])
            {
                pending[i] = urb;
                next_poll[i] = usb_time(NULL);
                urb->hcpriv = &pending[i];
                urb->actual_length = 0;
                ret = 0;
                rt_sem_release(&host_event);
                break;
            }
        }
    }
    else
    {
        ret = execute_urb(urb, urb->timeout);
    }

    rt_mutex_release(&host_lock);
    return ret;
}

int usbh_kill_urb(struct usbh_urb *urb)
{
    if (!urb)
    {
        return -USB_ERR_INVAL;
    }

    /* Synchronous transfers hold host_lock and are bounded. The caller cannot
     * free a class/URB until this lock confirms hardware retirement. */
    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    for (unsigned int i = 0; i < USB_ASYNC_SLOTS; ++i)
    {
        if (pending[i] == urb)
        {
            pending[i] = NULL;
        }
    }

    urb->hcpriv = NULL;
    urb->errorcode = -USB_ERR_SHUTDOWN;
    rt_mutex_release(&host_lock);
    return 0;
}

void USBH_IRQHandler(uint8_t busid)
{
    if (!busid)
    {
        usb_interrupt(USB_IRQ, NULL);
    }
}

static void usb_status(void)
{
    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);
    rt_kprintf("EHCI ready=%d quarantine=%d dma=0x%08x requests=%u errors=%u\n",
               controller.ready, controller.quarantined, controller.dma_address,
               requests, errors);
    if (operational)
    {
        rt_kprintf("cmd=%08x sts=%08x port=%08x (HS host; OHCI/OTG disabled)\n",
                   usb_read(NULL, LUSB_CMD), usb_read(NULL, LUSB_STS),
                   usb_read(NULL, LUSB_PORT));
    }

    rt_mutex_release(&host_lock);
}
MSH_CMD_EXPORT(usb_status, show LS2K0300 USB host state);
static long usb_list(int argc, char **argv)
{
    return lsusb(argc, argv);
}
/* FINSH stores every shell command through its legacy no-argument typedef. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
MSH_CMD_EXPORT_ALIAS(usb_list, lsusb, list enumerated USB devices);
#pragma GCC diagnostic pop

static int ls2k_usb_init(void)
{
    rt_ubase_t base = LOONGARCH_PHYS_UNCACHED(USB_BASE);
    uint32_t cap, parameters;
    rt_thread_t worker;

    rt_mutex_init(&host_lock, "usb_host", RT_IPC_FLAG_PRIO);
    rt_sem_init(&host_event, "usb_event", 0, RT_IPC_FLAG_PRIO);
    /* Do not reset the shared USB/OTG PHY or invent board VBUS GPIOs. */
    if ((HWREG32(LOONGARCH_PHYS_UNCACHED(0x16000114UL)) & 0x180) != 0x180 ||
        (HWREG32(LOONGARCH_PHYS_UNCACHED(0x1600011cUL)) & 0x180) != 0x180)
    {
        rt_kprintf("USB: firmware must enable USB/shared clocks and release reset\n");
        return -RT_ERROR;
    }

    cap = HWREG32(base);
    parameters = HWREG32(base + 4);
    if ((cap & 0xff) < 0x10 || (cap & 0xff) > 0x80 || (cap & 3) ||
        (cap >> 16) != 0x100 || (parameters & 15) != 1)
    {
        rt_kprintf("USB: unexpected EHCI capabilities %08x %08x\n", cap, parameters);
        return -RT_ERROR;
    }

    operational = base + (cap & 0xff);
    port_power_control = (parameters & 16) != 0;
    controller.io = &usb_io;
    controller.dma = (void *)LOONGARCH_PHYS_UNCACHED(LOONGARCH_ALIAS_PHYS(&usb_storage));
    rt_hw_interrupt_mask(USB_IRQ);
    rt_hw_interrupt_install(USB_IRQ, usb_interrupt, NULL, "usb_ehci");
    worker = rt_thread_create("usb_poll", usb_worker, NULL, 4096, 14, 10);
    if (!worker)
    {
        return -RT_ENOMEM;
    }

    /* g_usbhost_bus exists before the hub thread can enter usb_hc_init. */
    host_bus = &g_usbhost_bus[0];
    rt_thread_startup(worker);
    return usbh_initialize(0, base, NULL);
}
INIT_APP_EXPORT(ls2k_usb_init);
