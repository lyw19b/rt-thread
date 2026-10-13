/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-14     LoongsonLab  the first version
 */

#include <rtthread.h>

int dummy_whoami_cmd(int argc, char *argv[])
{
    rt_kprintf("\033[1m\033[32mI'm running on LoongArch64 Platform!\033[0m\n");
    return 0;
}

MSH_CMD_EXPORT_ALIAS(dummy_whoami_cmd, whoami, Print information about LoongsonLab.);


int dummy_exit_cmd(int argc, char *argv[])
{
    rt_kprintf("Exit System, Enter Loop\n");
    while (1);
    return 0;
}

MSH_CMD_EXPORT_ALIAS(dummy_exit_cmd, exit, Exit System and Enter Loop.);
