/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    add Xingyun Smart board setup
 *
 * Description:
 *   Mount the Smart temporary filesystem before launching user applications.
 */

#include <rthw.h>
#include <rtthread.h>
#include <klibc/kerrno.h>

#if defined(RT_USING_SMART) && defined(RT_USING_DFS_TMPFS) && defined(RT_USING_DFS_ROMFS)
#include <dfs_fs.h>
#include <dfs_romfs.h>

static int smart_filesystem_init(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root) != 0 ||
        dfs_mount(RT_NULL, "/mnt", "tmp", 0, RT_NULL) != 0)
    {
        rt_kprintf("Failed to mount Smart temporary filesystem\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_ENV_EXPORT(smart_filesystem_init);
#endif

#ifdef PKG_USING_LWEXT4

#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_mnt.h>
#include <ext4.h>

int mount_ssd(void)
{
    // 获取块设备
    struct blk_device *blkdev = rt_device_find("ls_ahci_blk");

    if (blkdev == RT_NULL)
    {
        rt_kprintf("mount device not found\n");
        return -RT_ERROR;
    }

    // 挂载块设备
    dfs_mount("ls_ahci_blk","/","ext",0,(void *)1);

    return 0;
}
INIT_ENV_EXPORT(mount_ssd);

#endif
