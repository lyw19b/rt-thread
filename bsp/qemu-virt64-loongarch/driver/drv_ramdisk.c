/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef BSP_USING_RAMDISK
#include <rtdevice.h>
#define RAMDISK_BASE    ((rt_uint8_t *)0x900000000c000000UL)
#define RAMDISK_SECTORS (64 * 1024 * 1024 / 512)

static struct rt_device ramdisk;

static rt_ssize_t ramdisk_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t count)
{
    if (pos < 0 || pos >= RAMDISK_SECTORS || count > RAMDISK_SECTORS - pos)
        return 0;
    rt_memcpy(buf, RAMDISK_BASE + pos * 512, count * 512);
    return count;
}

static rt_ssize_t ramdisk_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t count)
{
    if (pos < 0 || pos >= RAMDISK_SECTORS || count > RAMDISK_SECTORS - pos)
        return 0;
    rt_memcpy(RAMDISK_BASE + pos * 512, buf, count * 512);
    return count;
}

static rt_err_t ramdisk_control(rt_device_t dev, int cmd, void *arg)
{
    struct rt_device_blk_geometry *geometry = arg;
    if (cmd != RT_DEVICE_CTRL_BLK_GETGEOME || !arg)
        return -RT_EINVAL;
    geometry->bytes_per_sector = 512;
    geometry->block_size = 512;
    geometry->sector_count = RAMDISK_SECTORS;
    return RT_EOK;
}

static const struct rt_device_ops ramdisk_ops =
{
    .read = ramdisk_read,
    .write = ramdisk_write,
    .control = ramdisk_control,
};

static int ramdisk_init(void)
{
    ramdisk.type = RT_Device_Class_Block;
    ramdisk.ops = &ramdisk_ops;
    return rt_device_register(&ramdisk, "ramdisk0", RT_DEVICE_FLAG_RDWR);
}
INIT_DEVICE_EXPORT(ramdisk_init);
#endif
