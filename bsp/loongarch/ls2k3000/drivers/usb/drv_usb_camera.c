/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_usb_camera.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Bind the existing project UVC class to one native xHCI camera generation.
 * Notes:
 *   No CherryUSB core or second HCD owns the hardware. Its public class types
 *   describe the real device; control and ISO operations use our controller.
 *   Lock order is UVC camera_lock then controller lock. Attach/detach callbacks
 *   must run outside the controller lock and only from the USB owner thread.
 */

#include <rtthread.h>
#include "usbh_core.h"
#include "xhci_mouse.h"
#include "ls2k3000_usb.h"
#include "ls2k_usb_camera.h"

extern const struct usbh_class_info openlanpu_uvc_class;

static struct
{
    struct usbh_hubport port;
    struct ls_xhci *host;
    struct ls_xhci_device *device;
    uint32_t generation;
    uint8_t interface;
    uint8_t raw_config[LS_XHCI_BUFFER_BYTES];
    rt_bool_t attached;
    rt_tick_t last_attempt;
} camera;

static int usb_result(int result)
{
    if (result >= 0)
    {
        return result;
    }

    switch (result)
    {
    case LS_AGAIN:
    case LS_NACK:
        return -USB_ERR_NAK;
    case LS_DISABLED:
        return -USB_ERR_NODEV;
    case LS_TIMEOUT:
        return -USB_ERR_TIMEOUT;
    case LS_BUSY:
        return -USB_ERR_BUSY;
    case LS_INVALID:
        return -USB_ERR_INVAL;
    case LS_UNSUPPORTED:
        return -USB_ERR_NOTSUPP;
    default:
        return -USB_ERR_IO;
    }
}

/* Caller owns the controller lock; a stale class file cannot address a new slot. */
static rt_bool_t connected(struct usbh_hubport *port)
{
    return port == &camera.port && camera.host && camera.device &&
           camera.host->running && camera.device->connected &&
           camera.device->generation == camera.generation;
}

int usbh_control_transfer(struct usbh_hubport *port, struct usb_setup_packet *setup,
                           uint8_t *buffer)
{
    int result;

    if (!setup || (setup->wLength && !buffer))
    {
        return -USB_ERR_INVAL;
    }

    ls2k3000_usb_lock();
    result = connected(port) ?
        ls_xhci_control(camera.host, camera.device, setup->bmRequestType,
                         setup->bRequest, setup->wValue, setup->wIndex,
                         buffer, setup->wLength, 1000) : LS_DISABLED;
    ls2k3000_usb_unlock();
    return usb_result(result);
}

int usbh_set_interface(struct usbh_hubport *port, uint8_t interface, uint8_t alternate)
{
    struct usb_setup_packet setup = {
        .bmRequestType = 0x01,
        .bRequest = 11,
        .wValue = alternate,
        .wIndex = interface,
    };

    return usbh_control_transfer(port, &setup, RT_NULL);
}

int usbh_submit_urb(struct usbh_urb *urb)
{
    /* This board currently exposes only the ISO transport to the UVC class.
     * Never claim a successful Bulk URB without scheduling it in hardware. */
    if (urb)
    {
        urb->actual_length = 0;
    }

    return -USB_ERR_NOTSUPP;
}

int ls2k_usb_camera_start(struct usbh_hubport *port, uint8_t endpoint,
                          uint16_t packet, uint8_t interval)
{
    int result;

    ls2k3000_usb_lock();
    result = connected(port) ?
        ls_xhci_iso_start(camera.host, camera.device, endpoint, packet, interval) : LS_DISABLED;
    ls2k3000_usb_unlock();
    return usb_result(result);
}

int ls2k_usb_camera_stop(struct usbh_hubport *port)
{
    int result;

    ls2k3000_usb_lock();
    result = connected(port) ? ls_xhci_iso_stop(camera.host, camera.device) : LS_DISABLED;
    ls2k3000_usb_unlock();
    return usb_result(result);
}

int ls2k_usb_camera_read(struct usbh_hubport *port, uint8_t *data, size_t capacity)
{
    int result;

    ls2k3000_usb_lock();
    result = connected(port) ?
        ls_xhci_iso_read(camera.host, camera.device, data, capacity) : LS_DISABLED;
    ls2k3000_usb_unlock();
    return usb_result(result);
}

static int control_interface(const uint8_t *data, size_t bytes)
{
    for (size_t offset = 0; offset < bytes;)
    {
        if (bytes - offset < 2 || data[offset] < 2 || data[offset] > bytes - offset)
        {
            return -1;
        }

        if (data[offset + 1] == 4 && data[offset] >= 9 &&
            data[offset + 3] == 0 && data[offset + 5] == 14 && data[offset + 6] == 1)
        {
            return data[offset + 2] < CONFIG_USBHOST_MAX_INTERFACES ? data[offset + 2] : -1;
        }

        offset += data[offset];
    }

    return -1;
}

void ls2k3000_usb_camera_poll(struct ls_xhci *host)
{
    rt_bool_t present;

    if (camera.attached)
    {
        ls2k3000_usb_lock();
        present = connected(&camera.port);
        ls2k3000_usb_unlock();

        if (!present)
        {
            /* disconnect synchronizes with all UVC file operations before we
             * replace port fields. Transport wrappers reject the old generation. */
            openlanpu_uvc_class.class_driver->disconnect(&camera.port, camera.interface);
            camera.port.connected = false;
            camera.attached = RT_FALSE;
            camera.host = RT_NULL;
            camera.device = RT_NULL;
            rt_kprintf("xHCI UVC: detached\n");
        }
        else
        {
            return;
        }
    }

    if ((rt_tick_t)(rt_tick_get() - camera.last_attempt) < rt_tick_from_millisecond(100))
    {
        return;
    }

    ls2k3000_usb_lock();

    for (unsigned int index = 0; index < LS_XHCI_DEVICES; ++index)
    {
        struct ls_xhci_device *device = &host->devices[index];
        int interface;

        if (!host->running || !device->connected || device->kind != LS_XHCI_CLASS_CAMERA ||
            device->speed != 3 || device->raw_config_length < 9 ||
            device->raw_config_length > sizeof(camera.raw_config))
        {
            continue;
        }

        interface = control_interface(device->raw_config, device->raw_config_length);

        if (interface < 0 || device->raw_config_length !=
            (unsigned int)(device->raw_config[2] | (device->raw_config[3] << 8)))
        {
            continue;
        }

        camera.last_attempt = rt_tick_get();
        memset(&camera.port, 0, sizeof(camera.port));
        memcpy(camera.raw_config, device->raw_config, device->raw_config_length);
        camera.host = host;
        camera.device = device;
        camera.generation = device->generation;
        camera.interface = interface;
        camera.port.connected = true;
        camera.port.speed = USB_SPEED_HIGH;
        camera.port.port = device->port;
        camera.port.slot_id = device->slot;
        camera.port.device_desc.idVendor = device->vendor;
        camera.port.device_desc.idProduct = device->product;
        camera.port.raw_config_desc = camera.raw_config;
        memcpy(&camera.port.config.config_desc, camera.raw_config,
               sizeof(camera.port.config.config_desc));
        ls2k3000_usb_unlock();

        if (!openlanpu_uvc_class.class_driver->connect(&camera.port, camera.interface))
        {
            camera.attached = RT_TRUE;
        }

        return;
    }

    ls2k3000_usb_unlock();
}
