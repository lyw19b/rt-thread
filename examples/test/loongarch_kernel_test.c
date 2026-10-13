/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>

static struct rt_semaphore done;
static struct rt_mutex lock;
static struct rt_event event;
static struct rt_mailbox mailbox;
static struct rt_messagequeue queue;
static volatile int failures;
static volatile int shared_count;
static volatile int timer_count;
static rt_ubase_t mailbox_pool[4];
static rt_uint8_t queue_pool[128];

#define CHECK(expr) do { if (!(expr)) { rt_kprintf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)

static void timer_entry(void *arg)
{
    timer_count++;
}

static void worker(void *arg)
{
    int i;
    rt_ubase_t id = (rt_ubase_t)arg;
    register double value __asm__("$f24") = (double)id + 0.5;

    for (i = 0; i < 200; i++)
    {
        /* Keep an FPR live across both timer preemption and voluntary switches. */
        __asm__ volatile("" : "+f"(value));
        rt_tick_t start = rt_tick_get();
        while (rt_tick_get() == start) {}
        rt_mutex_take(&lock, RT_WAITING_FOREVER);
        shared_count++;
        rt_mutex_release(&lock);
        rt_thread_mdelay(1);
        __asm__ volatile("" : "+f"(value));
        CHECK(value == (double)id + 0.5);
    }
    rt_event_send(&event, 1UL << id);
    rt_sem_release(&done);
}

static int port_test(void)
{
    rt_thread_t first, second;
    rt_timer_t timer;
    rt_uint32_t received;
    rt_ubase_t message;
    unsigned int sent = 0x12345678, readback = 0;
    void *blocks[32];
    rt_tick_t before;
    int i;

    failures = shared_count = timer_count = 0;
    CHECK(rt_sem_init(&done, "done", 0, RT_IPC_FLAG_FIFO) == RT_EOK);
    CHECK(rt_mutex_init(&lock, "lock", RT_IPC_FLAG_PRIO) == RT_EOK);
    CHECK(rt_event_init(&event, "event", RT_IPC_FLAG_FIFO) == RT_EOK);
    CHECK(rt_mb_init(&mailbox, "mb", mailbox_pool, 4, RT_IPC_FLAG_FIFO) == RT_EOK);
    CHECK(rt_mq_init(&queue, "mq", queue_pool, sizeof(sent), sizeof(queue_pool), RT_IPC_FLAG_FIFO) == RT_EOK);
    CHECK(rt_mb_send(&mailbox, 0x9876) == RT_EOK);
    CHECK(rt_mb_recv(&mailbox, &message, 0) == RT_EOK && message == 0x9876);
    CHECK(rt_mq_send(&queue, &sent, sizeof(sent)) == RT_EOK);
    CHECK(rt_mq_recv(&queue, &readback, sizeof(readback), 0) == sizeof(readback) && readback == sent);
    before = rt_tick_get();
    CHECK(rt_sem_take(&done, 5) == -RT_ETIMEOUT);
    CHECK(rt_tick_get() - before >= 5);

    for (i = 0; i < 32; i++)
    {
        blocks[i] = rt_malloc(17 + 31 * i);
        CHECK(blocks[i] != RT_NULL);
        if (blocks[i]) rt_memset(blocks[i], i, 17 + 31 * i);
    }
    for (i = 31; i >= 0; i--)
    {
        if (blocks[i])
        {
            CHECK(((rt_uint8_t *)blocks[i])[16 + 31 * i] == i);
            rt_free(blocks[i]);
        }
    }
    timer = rt_timer_create("testtm", timer_entry, RT_NULL, 2,
                           RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    CHECK(timer != RT_NULL);
    if (timer) rt_timer_start(timer);
    first = rt_thread_create("fp0", worker, (void *)0, 4096, 15, 2);
    second = rt_thread_create("fp1", worker, (void *)1, 4096, 15, 2);
    CHECK(first && second);
    if (first) rt_thread_startup(first);
    if (second) rt_thread_startup(second);
    CHECK(rt_sem_take(&done, RT_TICK_PER_SECOND * 10) == RT_EOK);
    CHECK(rt_sem_take(&done, RT_TICK_PER_SECOND * 10) == RT_EOK);
    CHECK(shared_count == 400);
    CHECK(rt_event_recv(&event, 3, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        0, &received) == RT_EOK && received == 3);
    CHECK(timer_count > 10);
    if (timer) rt_timer_delete(timer);
    rt_thread_mdelay(10);
    rt_mq_detach(&queue);
    rt_mb_detach(&mailbox);
    rt_event_detach(&event);
    rt_mutex_detach(&lock);
    rt_sem_detach(&done);
    rt_kprintf("KERNEL PORT TEST: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? -1 : 0;
}
MSH_CMD_EXPORT(port_test, Test scheduling IPC timers heap and floating point context);
