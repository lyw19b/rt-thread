/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef RT_USING_DFS_TMPFS
#include <dfs_fs.h>
#ifdef RT_USING_DFS_ROMFS
#include <dfs_romfs.h>
#endif

static int filesystem_init(void)
{
#ifdef RT_USING_DFS_ROMFS
    if (dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root) != 0 ||
        dfs_mount(RT_NULL, "/mnt", "tmp", 0, RT_NULL) != 0)
#else
    if (dfs_mount(RT_NULL, "/", "tmp", 0, RT_NULL) != 0)
#endif
    {
        rt_kprintf("Failed to mount temporary root filesystem\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_ENV_EXPORT(filesystem_init);
#endif
