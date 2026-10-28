/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               launch embedded UI directly from ROMFS
 */

/*
 * Description:
 *   Start NovaUIOS as a Smart user process without a second in-memory ELF file.
 * Notes:
 *   The board ROMFS retains the embedded bytes for the process lifetime.
 */

#include <rtthread.h>

#ifdef BSP_USING_NOVAUI
#include <rtdevice.h>
#include <lwp.h>
#include <lwp_pid.h>

static char image_path[] = "/bin/novaui.elf";

static int novaui_start(void)
{
    char *argv[] = {image_path, RT_NULL};
    char *envp[] = {RT_NULL};
    int pid;

    if (!rt_device_find("novaui"))
    {
        return -RT_ENOSYS;
    }

    if (lwp_name2pid("novaui.elf") > 0)
    {
        return -RT_EBUSY;
    }

    pid = lwp_execve(image_path, 0, 1, argv, envp);
    rt_kprintf("NOVAUI process pid=%d mode=user image=%s\n", pid, image_path);
    return pid > 0 ? RT_EOK : pid;
}

MSH_CMD_EXPORT(novaui_start, Start the embedded Smart user interface);
INIT_APP_EXPORT(novaui_start);
#endif
