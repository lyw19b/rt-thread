/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-25     OpenLANPU Contributors    add bounded high-address USB host support
 *
 */

/* Description: Board-only SMP critical sections and millisecond soft timers. */
#include <rtthread.h>
#include <rthw.h>
#include "usb_osal.h"

static struct rt_spinlock usb_stack_lock;

static int usb_osal_board_init(void)
{
    rt_spin_lock_init(&usb_stack_lock);
    return RT_EOK;
}
INIT_PREV_EXPORT(usb_osal_board_init);

size_t usb_osal_enter_critical_section(void)
{
    return rt_spin_lock_irqsave(&usb_stack_lock);
}

void usb_osal_leave_critical_section(size_t flags)
{
    rt_spin_unlock_irqrestore(&usb_stack_lock, flags);
}

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t timeout_ms,
                                            usb_timer_handler_t handler,
                                            void *argument, bool periodic)
{
    struct usb_osal_timer *timer = rt_calloc(1, sizeof(*timer));
    rt_tick_t ticks = rt_tick_from_millisecond(timeout_ms);

    if (!timer)
    {
        return NULL;
    }

    if (!ticks)
    {
        ticks = 1;
    }

    timer->timer = rt_timer_create(name, handler, argument, ticks,
                                  RT_TIMER_FLAG_SOFT_TIMER |
                                  (periodic ? RT_TIMER_FLAG_PERIODIC : RT_TIMER_FLAG_ONE_SHOT));
    if (!timer->timer)
    {
        rt_free(timer);
        return NULL;
    }

    return timer;
}
