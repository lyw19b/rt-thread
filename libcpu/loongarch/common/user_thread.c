/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               reclaim legacy Smart user stacks
 */

/*
 * Description:
 *   Release kernel-owned user stacks during deferred LWP thread cleanup.
 *   Clone threads have no kernel-owned stack recorded in user_stack; their
 *   caller-owned mappings remain the responsibility of the user runtime.
 */

#include <rtthread.h>
#include <lwp.h>
#include <lwp_user_mm.h>

void __real_lwp_cleanup(struct rt_thread *thread);

/**
 * @brief Release a retired thread's owned mapping before dropping its LWP reference.
 *
 * @note The defunct reaper invokes cleanup only after the thread has left its
 *       CPU. Unmapping here also leaves robust-futex and child-TID exit handling
 *       access to the user stack until they have completed.
 */
void __wrap_lwp_cleanup(struct rt_thread *thread)
{
    if (thread && thread->lwp && thread->user_stack && thread->user_stack_size)
    {
        int result = lwp_unmap_user(thread->lwp, thread->user_stack);

        if (result == RT_EOK)
        {
            thread->user_stack = RT_NULL;
            thread->user_stack_size = 0;
        }
        else
        {
            rt_kprintf("LoongArch: user stack cleanup failed: %d\n", result);
        }
    }

    __real_lwp_cleanup(thread);
}
