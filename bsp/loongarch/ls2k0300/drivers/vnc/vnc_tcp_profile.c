/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    optional BSP-owned TCP timing probes
 */
#include <rtthread.h>
#include <lwip/tcp.h>

static uint64_t write_ticks, output_ticks, allocation_ticks;
static uint32_t writes, outputs, allocations;

extern err_t __real_tcp_write(struct tcp_pcb *, const void *, u16_t, u8_t);
extern err_t __real_tcp_output(struct tcp_pcb *);
extern void *__real_rt_malloc(rt_size_t);

err_t __wrap_tcp_write(struct tcp_pcb *pcb, const void *data, u16_t size, u8_t flags)
{
    rt_tick_t before = rt_tick_get();
    err_t result = __real_tcp_write(pcb, data, size, flags);

    __atomic_fetch_add(&write_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
    __atomic_fetch_add(&writes, 1, __ATOMIC_RELAXED);
    return result;
}

err_t __wrap_tcp_output(struct tcp_pcb *pcb)
{
    rt_tick_t before = rt_tick_get();
    err_t result = __real_tcp_output(pcb);

    __atomic_fetch_add(&output_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
    __atomic_fetch_add(&outputs, 1, __ATOMIC_RELAXED);
    return result;
}

void *__wrap_rt_malloc(rt_size_t size)
{
    rt_tick_t before = rt_tick_get();
    void *result = __real_rt_malloc(size);

    __atomic_fetch_add(&allocation_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
    __atomic_fetch_add(&allocations, 1, __ATOMIC_RELAXED);
    return result;
}

void vnc_tcp_profile_report(void)
{
    rt_kprintf("VNC TCP ticks write=%llu output=%llu malloc=%llu calls=%u/%u/%u\n",
        (unsigned long long)__atomic_load_n(&write_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&output_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&allocation_ticks, __ATOMIC_RELAXED),
        __atomic_load_n(&writes, __ATOMIC_RELAXED),
        __atomic_load_n(&outputs, __ATOMIC_RELAXED),
        __atomic_load_n(&allocations, __ATOMIC_RELAXED));
}
