/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <stdlib.h>

static struct rt_semaphore done;
static struct rt_mutex lock;
static struct rt_event event;
static struct rt_mailbox mailbox;
static struct rt_messagequeue queue;
static volatile int failures;
static volatile int shared_count;
static volatile int timer_count;
static int iterations;
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

    for (i = 0; i < iterations; i++)
    {
        rt_size_t size = 17 + ((i * 31 + id) % 2048);
        unsigned char *block = rt_malloc(size);
        unsigned int sent = i, received = ~sent;
        rt_ubase_t ack = ~sent;
        rt_size_t offset;

        CHECK(block != RT_NULL);
        if (block)
            rt_memset(block, i & 255, size);
        if (!id)
        {
            CHECK(rt_mq_send_wait(&queue, &sent, sizeof(sent), 1000) == RT_EOK);
            CHECK(rt_mb_recv(&mailbox, &ack, 1000) == RT_EOK && ack == sent);
        }
        else
        {
            CHECK(rt_mq_recv(&queue, &received, sizeof(received), 1000) == sizeof(received));
            CHECK(received == sent);
            CHECK(rt_mb_send_wait(&mailbox, received, 1000) == RT_EOK);
        }
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
        if (block)
        {
            for (offset = 0; offset < size; offset++)
                CHECK(block[offset] == (i & 255));
            rt_free(block);
        }
    }
    rt_event_send(&event, 1UL << id);
    rt_sem_release(&done);
}

static int port_test(int argc, char **argv)
{
    rt_thread_t first, second;
    rt_timer_t timer;
    rt_uint32_t received;
    rt_ubase_t message;
    unsigned int sent = 0x12345678, readback = 0;
    void *blocks[32];
    rt_tick_t before;
    int i;
    rt_size_t total, used_before, used_after, maximum;

    iterations = argc > 1 ? atoi(argv[1]) : 200;
    if (iterations < 1 || iterations > 1000000)
        return -RT_EINVAL;
    rt_memory_info(&total, &used_before, &maximum);
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
    if (!first || !second)
    {
        if (first) rt_thread_delete(first);
        if (second) rt_thread_delete(second);
        goto cleanup;
    }
    rt_thread_startup(first);
    rt_thread_startup(second);
    for (i = 0; i < 2; i++)
    {
        if (rt_sem_take(&done, RT_TICK_PER_SECOND * 10 + iterations * 4) != RT_EOK)
        {
            /* Keep IPC objects alive if a stalled worker could still use them. */
            rt_kprintf("KERNEL PORT TEST: FAIL (worker timeout)\n");
            return -RT_ETIMEOUT;
        }
    }
    CHECK(shared_count == iterations * 2);
    CHECK(rt_event_recv(&event, 3, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        0, &received) == RT_EOK && received == 3);
    CHECK(timer_count > 0);
cleanup:
    if (timer) rt_timer_delete(timer);
    rt_thread_mdelay(50);
    rt_mq_detach(&queue);
    rt_mb_detach(&mailbox);
    rt_event_detach(&event);
    rt_mutex_detach(&lock);
    rt_sem_detach(&done);
    rt_memory_info(&total, &used_after, &maximum);
    CHECK(used_after == used_before);
    rt_kprintf("KERNEL iterations=%d switches>=%d heap=%lu/%lu ticks=%lu\n",
               iterations, shared_count, used_before, used_after, rt_tick_get() - before);
    rt_kprintf("KERNEL PORT TEST: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? -1 : 0;
}
MSH_CMD_EXPORT(port_test, Test scheduling IPC timers heap and floating point context);

#ifdef RT_USING_SMART
#include <mm_page.h>

static void port_resources(void)
{
    rt_size_t total, used, maximum, pages, free_pages;
    rt_memory_info(&total, &used, &maximum);
    rt_page_get_info(&pages, &free_pages);
    rt_kprintf("RESOURCES heap=%lu pages=%lu/%lu\n", used, free_pages, pages);
}
MSH_CMD_EXPORT(port_resources, Show heap and free pages for leak checks);
#endif
