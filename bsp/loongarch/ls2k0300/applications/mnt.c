/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef RT_USING_DFS_TMPFS
#include <dfs_fs.h>

static int filesystem_init(void)
{
    if (dfs_mount(RT_NULL, "/", "tmp", 0, RT_NULL) != 0)
    {
        rt_kprintf("Failed to mount temporary root filesystem\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(filesystem_init);
#endif
