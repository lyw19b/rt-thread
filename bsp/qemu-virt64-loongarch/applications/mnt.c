/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef RT_USING_DFS
#include <dfs_fs.h>

static int filesystem_init(void)
{
    const char *device = rt_device_find("virtio-blk0") ? "virtio-blk0" : "ramdisk0";
    if (dfs_mount(device, "/", "elm", 0, RT_NULL) != 0)
    {
        rt_kprintf("Failed to mount root filesystem\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(filesystem_init);
#endif
