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
 *   Expose the first supported NVMe namespace as a serialized RT-Thread device.
 */

#include <rtdevice.h>
#include <rthw.h>
#include "board.h"
#include "ls2k3000_pci.h"
#include "nvme_poll.h"

static struct ls_nvme controller;
static struct rt_mutex io_lock;
static struct rt_device disk;
static rt_ubase_t registers;

static uint32_t nvme_read32(void *context, uint32_t offset)
{
    RT_UNUSED(context);
    return *(volatile uint32_t *)(registers + offset);
}

static void nvme_write32(void *context, uint32_t offset, uint32_t value)
{
    RT_UNUSED(context);
    *(volatile uint32_t *)(registers + offset) = value;
    rt_hw_dmb();
}

static void nvme_barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static void nvme_sleep(void *context, unsigned int milliseconds)
{
    RT_UNUSED(context);
    rt_thread_mdelay(milliseconds);
}

static const struct ls_nvme_ops hardware_ops = {
    .read32 = nvme_read32,
    .write32 = nvme_write32,
    .barrier = nvme_barrier,
    .sleep_ms = nvme_sleep,
};

static rt_ssize_t block_transfer(rt_off_t position, void *buffer, rt_size_t count, int write)
{
    rt_size_t completed = 0;
    int result = 0;

    if (position < 0 || (!buffer && count) || (uint64_t)position > controller.sectors ||
        count > controller.sectors - (uint64_t)position ||
        !controller.sector_bytes || count > (rt_size_t)PTRDIFF_MAX / controller.sector_bytes)
    {
        return -RT_EINVAL;
    }

    if (rt_mutex_take(&io_lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }

    while (completed < count)
    {
        result = ls_nvme_transfer(&controller, position + completed,
                                  (uint8_t *)buffer + completed * controller.sector_bytes,
                                  write);

        if (result)
        {
            break;
        }

        completed++;
    }

    rt_mutex_release(&io_lock);

    if (result)
    {
        rt_set_errno(result == LS_NVME_TIMEOUT ? -RT_ETIMEOUT : -RT_EIO);
    }

    return completed;
}

static rt_ssize_t block_read(rt_device_t device, rt_off_t position, void *buffer, rt_size_t count)
{
    RT_UNUSED(device);
    return block_transfer(position, buffer, count, 0);
}

static rt_ssize_t block_write(rt_device_t device, rt_off_t position,
                              const void *buffer, rt_size_t count)
{
    RT_UNUSED(device);

#ifdef BSP_NVME_ALLOW_WRITE
    return block_transfer(position, (void *)buffer, count, 1);
#else
    RT_UNUSED(position);
    RT_UNUSED(buffer);
    RT_UNUSED(count);
    return -RT_ENOSYS;
#endif
}

static rt_err_t block_control(rt_device_t device, int command, void *argument)
{
    RT_UNUSED(device);

    if (command == RT_DEVICE_CTRL_BLK_GETGEOME && argument)
    {
        struct rt_device_blk_geometry *geometry = argument;

        geometry->sector_count = controller.sectors;
        geometry->bytes_per_sector = controller.sector_bytes;
        geometry->block_size = controller.sector_bytes;
        return RT_EOK;
    }

    if (command == RT_DEVICE_CTRL_BLK_SYNC)
    {
        int result;

        if (rt_mutex_take(&io_lock, RT_WAITING_FOREVER) != RT_EOK)
        {
            return -RT_ERROR;
        }

        result = ls_nvme_flush(&controller);
        rt_mutex_release(&io_lock);
        return result ? -RT_EIO : RT_EOK;
    }

    return -RT_ENOSYS;
}

static const struct rt_device_ops block_ops = {
    .read = block_read,
    .write = block_write,
    .control = block_control,
};

static int nvme_init(void)
{
    const struct ls2k3000_pci_device *device = ls2k3000_pci_find(0x010802, 0);
    int result;

    if (!device)
    {
        rt_kprintf("NVMe: no controller in firmware PCI inventory\n");
        return -RT_ENOSYS;
    }

    result = ls2k3000_pci_map(device, 0, 0x1010, &registers);

    if (result)
    {
        return result;
    }

    for (unsigned int index = 0; index < 2; ++index)
    {
        struct ls_nvme_queue *queue = &controller.queue[index];

        queue->submission = ls2k3000_dma_alloc(LS_NVME_PAGE, LS_NVME_PAGE,
                                              &queue->submission_dma);
        queue->completion = ls2k3000_dma_alloc(LS_NVME_PAGE, LS_NVME_PAGE,
                                              &queue->completion_dma);

        if (!queue->submission || !queue->completion)
        {
            return -RT_ENOMEM;
        }
    }

    controller.bounce = ls2k3000_dma_alloc(LS_NVME_PAGE, LS_NVME_PAGE,
                                          &controller.bounce_dma);

    if (!controller.bounce)
    {
        return -RT_ENOMEM;
    }

    controller.ops = &hardware_ops;
    controller.bar_bytes = device->bar[0].bytes;
    result = rt_mutex_init(&io_lock, "nvme", RT_IPC_FLAG_PRIO);

    if (result)
    {
        return result;
    }

    /* Keep legacy INTx disabled; native queues are polled with MSI unused. */
    result = ls2k3000_pci_command(device, (1U << 10) | 6, 0);

    if (!result)
    {
        result = ls_nvme_start(&controller);
    }

    if (result)
    {
        rt_kprintf("NVMe: initialization failed (%d), DMA remains reserved\n", result);
        ls2k3000_pci_command(device, 1U << 10, 4);
        rt_mutex_detach(&io_lock);
        return -RT_EIO;
    }

    disk.type = RT_Device_Class_Block;
    disk.ops = &block_ops;

#ifdef BSP_NVME_ALLOW_WRITE
    result = rt_device_register(&disk, "nvme0", RT_DEVICE_FLAG_RDWR);
#else
    result = rt_device_register(&disk, "nvme0", RT_DEVICE_FLAG_RDONLY);
#endif

    if (result)
    {
        ls_nvme_stop(&controller);
        ls2k3000_pci_command(device, 1U << 10, 4);
        rt_mutex_detach(&io_lock);
        return result;
    }

    rt_kprintf("NVMe: nvme0 ns=%u sectors=%lu sector_bytes=%u\n",
               controller.namespace_id, (unsigned long)controller.sectors,
               controller.sector_bytes);
    return RT_EOK;
}

INIT_DEVICE_EXPORT(nvme_init);
