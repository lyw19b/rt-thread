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
 *   Register USB disks as usb0..usb3 without automounting or formatting media.
 * Notes:
 *   Static device records survive surprise removal and outstanding opens.
 *   A per-disk mutex protects the entire BOT transaction and class lifetime.
 *   Only 512-byte sectors and READ/WRITE(10) capacity are exposed initially.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "usbh_core.h"
#include "usbh_msc.h"
#include "ls2k_usb_bot.h"

struct usb_disk
{
    struct rt_device device;
    struct rt_mutex lock;
    struct usbh_msc *msc;
    struct ls2k_usb_bot bot;
    uint32_t sectors;
    bool registered, initialized, opened;
};

static struct usb_disk disks[CONFIG_USBHOST_MAX_MSC_CLASS];

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int disk_transfer(void *context, bool input, uint8_t *buffer, uint32_t length)
{
    struct usb_disk *disk = context;
    struct usbh_msc *msc = disk->msc;
    struct usbh_urb *urb = input ? &msc->bulkin_urb : &msc->bulkout_urb;
    int ret;

    usbh_bulk_urb_fill(urb, msc->hport, input ? msc->bulkin : msc->bulkout,
                       buffer, length, CONFIG_USBHOST_MSC_TIMEOUT, NULL, NULL);
    ret = usbh_submit_urb(urb);
    return ret ? ret : (int)urb->actual_length;
}

static rt_ssize_t disk_io(rt_device_t dev, rt_off_t position, void *buffer,
                          rt_size_t count, bool input)
{
    struct usb_disk *disk = dev->user_data;
    rt_size_t completed = 0;

    rt_mutex_take(&disk->lock, RT_WAITING_FOREVER);
    if (!disk->msc || !buffer || position < 0 || (uint64_t)position > disk->sectors ||
        count > disk->sectors - (uint64_t)position)
    {
        goto out;
    }

    while (completed < count)
    {
        uint8_t cdb[10] = {0};
        uint32_t lba = (uint32_t)position + completed;
        uint32_t blocks = count - completed > 128 ? 128 : count - completed;
        int ret;

        cdb[0] = input ? 0x28 : 0x2a;
        /* Flush requests use SYNCHRONIZE CACHE via the block control API. */
        cdb[2] = lba >> 24;
        cdb[3] = lba >> 16;
        cdb[4] = lba >> 8;
        cdb[5] = lba;
        cdb[7] = blocks >> 8;
        cdb[8] = blocks;
        ret = ls2k_usb_bot_command(&disk->bot, cdb, sizeof(cdb), input,
                                   (uint8_t *)buffer + completed * 512, blocks * 512);
        if (ret)
        {
            break;
        }

        completed += blocks;
    }

out:
    rt_mutex_release(&disk->lock);
    return completed;
}

static rt_ssize_t disk_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    return disk_io(dev, pos, buffer, size, true);
}

static rt_ssize_t disk_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    return disk_io(dev, pos, (void *)buffer, size, false);
}

static rt_err_t disk_control(rt_device_t dev, int command, void *args)
{
    struct usb_disk *disk = dev->user_data;
    rt_err_t ret = -RT_EINVAL;

    rt_mutex_take(&disk->lock, RT_WAITING_FOREVER);
    if (!disk->msc)
    {
        ret = -RT_EIO;
    }
    else if (command == RT_DEVICE_CTRL_BLK_GETGEOME && args)
    {
        struct rt_device_blk_geometry *geometry = args;

        geometry->bytes_per_sector = 512;
        geometry->block_size = 512;
        geometry->sector_count = disk->sectors;
        ret = RT_EOK;
    }
    else if (command == RT_DEVICE_CTRL_BLK_SYNC)
    {
        uint8_t cdb[10] = {0x35};

        ret = ls2k_usb_bot_command(&disk->bot, cdb, sizeof(cdb), false, NULL, 0) ?
              -RT_EIO : RT_EOK;
    }

    rt_mutex_release(&disk->lock);
    return ret;
}

static rt_err_t disk_open(rt_device_t dev, rt_uint16_t flags)
{
    struct usb_disk *disk = dev->user_data;
    rt_err_t ret;

    RT_UNUSED(flags);
    rt_mutex_take(&disk->lock, RT_WAITING_FOREVER);
    ret = !disk->msc ? -RT_EIO : (disk->opened ? -RT_EBUSY : RT_EOK);
    if (!ret)
    {
        disk->opened = true;
    }
    rt_mutex_release(&disk->lock);
    return ret;
}

static rt_err_t disk_close(rt_device_t dev)
{
    struct usb_disk *disk = dev->user_data;

    rt_mutex_take(&disk->lock, RT_WAITING_FOREVER);
    disk->opened = false;
    rt_mutex_release(&disk->lock);
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops disk_ops =
{
    NULL, disk_open, disk_close, disk_read, disk_write, disk_control
};
#endif

void usbh_msc_run(struct usbh_msc *msc)
{
    struct usb_disk *disk = NULL;
    uint8_t capacity[8], sense[18];
    uint8_t ready[6] = {0};
    uint8_t request_sense[6] = {3, 0, 0, 0, sizeof(sense), 0};
    uint8_t read_capacity[10] = {0x25};
    char name[RT_NAME_MAX];
    int ret = -USB_ERR_IO;

    for (unsigned int i = 0; i < CONFIG_USBHOST_MAX_MSC_CLASS; ++i)
    {
        struct usb_disk *candidate = &disks[i];

        if (!candidate->initialized)
        {
            rt_mutex_init(&candidate->lock, "usb_disk", RT_IPC_FLAG_PRIO);
            candidate->initialized = true;
        }

        rt_mutex_take(&candidate->lock, RT_WAITING_FOREVER);
        if (!candidate->msc && !candidate->opened)
        {
            disk = candidate;
            rt_snprintf(name, sizeof(name), "usb%u", i);
            break;
        }

        rt_mutex_release(&candidate->lock);
    }

    if (!disk)
    {
        rt_kprintf("USB disk: all device slots remain in use\n");
        return;
    }

    disk->msc = msc;
    disk->bot = (struct ls2k_usb_bot){disk, disk_transfer, 0, false};
    for (unsigned int retry = 0; retry < 5; ++retry)
    {
        ret = ls2k_usb_bot_command(&disk->bot, ready, sizeof(ready), false, NULL, 0);
        if (!ret || disk->bot.fault)
        {
            break;
        }

        ls2k_usb_bot_command(&disk->bot, request_sense, sizeof(request_sense),
                             true, sense, sizeof(sense));
        rt_thread_mdelay(100);
    }

    if (!ret)
    {
        ret = ls2k_usb_bot_command(&disk->bot, read_capacity, sizeof(read_capacity),
                                   true, capacity, sizeof(capacity));
    }

    if (ret || be32(capacity + 4) != 512 || be32(capacity) == UINT32_MAX)
    {
        rt_kprintf("USB disk: capacity/readiness unsupported or failed (%d)\n", ret);
        disk->msc = NULL;
        rt_mutex_release(&disk->lock);
        return;
    }

    disk->sectors = be32(capacity) + 1;
    if (!disk->registered)
    {
        disk->device.type = RT_Device_Class_Block;
        disk->device.user_data = disk;
#ifdef RT_USING_DEVICE_OPS
        disk->device.ops = &disk_ops;
#else
        disk->device.open = disk_open;
        disk->device.close = disk_close;
        disk->device.read = disk_read;
        disk->device.write = disk_write;
        disk->device.control = disk_control;
#endif
        ret = rt_device_register(&disk->device, name, RT_DEVICE_FLAG_RDWR |
                                 RT_DEVICE_FLAG_REMOVABLE | RT_DEVICE_FLAG_STANDALONE);
        if (ret)
        {
            disk->msc = NULL;
            rt_mutex_release(&disk->lock);
            return;
        }

        disk->registered = true;
    }

    msc->user_data = disk;
    rt_kprintf("USB disk %s: %u sectors x 512; mount manually\n", name, disk->sectors);
    rt_mutex_release(&disk->lock);
}

void usbh_msc_stop(struct usbh_msc *msc)
{
    struct usb_disk *disk = msc->user_data;

    if (!disk)
    {
        return;
    }

    rt_mutex_take(&disk->lock, RT_WAITING_FOREVER);
    disk->msc = NULL;
    disk->sectors = 0;
    disk->bot.fault = true;
    msc->user_data = NULL;
    rt_mutex_release(&disk->lock);
    rt_kprintf("USB disk removed: open handles are offline; unmount before reuse\n");
}
