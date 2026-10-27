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
 *   Mount a temporary UI filesystem without accessing persistent disks.
 */

#include <rtthread.h>

#ifdef BSP_USING_NOVAUI
#include <dfs_fs.h>
#include <dfs_romfs.h>

static int filesystem_init(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root) != 0 ||
        dfs_mount(RT_NULL, "/mnt", "tmp", 0, RT_NULL) != 0)
    {
        rt_kprintf("NOVAUI: cannot mount temporary filesystem\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}

INIT_ENV_EXPORT(filesystem_init);
#endif
