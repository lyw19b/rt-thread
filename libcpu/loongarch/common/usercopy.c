/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     LoongsonLab               preserve adjacent SMP user memory writes
 */

/*
 * Description:
 *   Replace whole-page read/modify/write user copies with bounded RAM writes.
 * Notes:
 *   GNU ld wrapping is enabled only for LoongArch Smart. lwp_user_mm.c calls
 *   its own helpers directly, so wrapping lwp_data_put alone is insufficient.
 *   Keep RT-Thread's public source and the existing copy-from-user path intact.
 */

#include <rtthread.h>
#include <lwp.h>
#include <lwp_internal.h>
#include <lwp_user_mm.h>
#include "mmu.h"

void *__real_lwp_memcpy(void *destination, const void *source, size_t size);

size_t __wrap_lwp_data_put(struct rt_lwp *lwp, void *destination,
                           void *source, size_t size)
{
    if (!lwp || !source)
    {
        return 0;
    }

    return loongarch_user_write(lwp->aspace, destination, source, size, -1);
}

size_t __wrap_lwp_put_to_user(void *destination, void *source, size_t size)
{
    return __wrap_lwp_data_put(lwp_self(), destination, source, size);
}

size_t __wrap_lwp_data_set(struct rt_lwp *lwp, void *destination,
                           int value, size_t size)
{
    if (!lwp)
    {
        return 0;
    }

    return loongarch_user_write(lwp->aspace, destination, RT_NULL, size,
                                (rt_uint8_t)value);
}

void *__wrap_lwp_memcpy(void *destination, const void *source, size_t size)
{
    if (lwp_in_user_space(destination) && !lwp_in_user_space((void *)source))
    {
        __wrap_lwp_put_to_user(destination, (void *)source, size);
        return destination;
    }

    /* Preserve the original kernel/user read and unsupported user/user rules.
     * This branch never invokes the generic kernel-to-user copy path. */
    return __real_lwp_memcpy(destination, source, size);
}
