/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-10-23     OpenLANPU    Add an isolated procfs SMP diagnostic
 */

#include <rtthread.h>

#if defined(BSP_USING_SELFTEST) && defined(RT_USING_SMP) && \
    defined(RT_USING_DFS_PROCFS) && RT_CPUS_NR > 1

#include <proc.h>
#include <rthw.h>
#include <stdlib.h>

#define PROC_PROBE_QUEUE_SIZE 64U

static struct proc_dentry *probe_queue[PROC_PROBE_QUEUE_SIZE];
static struct proc_dentry *probe_parent;
static struct rt_semaphore probe_done;
static struct rt_mutex probe_lock;
static unsigned int probe_head;
static unsigned int probe_tail;
static unsigned int probe_ready;
static unsigned int probe_finished;
static unsigned int probe_active;
static unsigned int probe_errors;
static unsigned int probe_rounds;
static rt_bool_t probe_serialized;

static void probe_lock_take(void)
{
    if (probe_serialized)
    {
        rt_mutex_take(&probe_lock, RT_WAITING_FOREVER);
    }
}

static void probe_lock_release(void)
{
    if (probe_serialized)
    {
        rt_mutex_release(&probe_lock);
    }
}

static void probe_wait_for_peer(unsigned int cpu)
{
    __atomic_fetch_or(&probe_ready, 1U << cpu, __ATOMIC_RELEASE);

    while (__atomic_load_n(&probe_ready, __ATOMIC_ACQUIRE) != 3U)
    {
        rt_thread_mdelay(1);
    }
}

static void probe_create(void *parameter)
{
    unsigned int iteration;

    RT_UNUSED(parameter);
    probe_wait_for_peer(0);

    for (iteration = 0; iteration < probe_rounds; iteration++)
    {
        struct proc_dentry *entry;
        char name[24];

        while (iteration - __atomic_load_n(&probe_tail, __ATOMIC_ACQUIRE) >=
               PROC_PROBE_QUEUE_SIZE)
        {
            rt_thread_mdelay(1);
        }

        rt_snprintf(name, sizeof(name), "entry-%u", iteration);
        probe_lock_take();
        entry = proc_mkdir(name, probe_parent);
        probe_lock_release();

        if (entry == RT_NULL)
        {
            __atomic_fetch_add(&probe_errors, 1, __ATOMIC_RELAXED);
            break;
        }

        /* Transfer the caller reference after publishing the new directory. */
        probe_queue[iteration % PROC_PROBE_QUEUE_SIZE] = entry;
        __atomic_store_n(&probe_head, iteration + 1, __ATOMIC_RELEASE);
    }

    __atomic_store_n(&probe_finished, 1, __ATOMIC_RELEASE);
    rt_sem_release(&probe_done);
}

static void probe_remove(void *parameter)
{
    unsigned int iteration = 0;

    RT_UNUSED(parameter);
    probe_wait_for_peer(1);

    for (;;)
    {
        unsigned int head = __atomic_load_n(&probe_head, __ATOMIC_ACQUIRE);

        if (iteration == head)
        {
            if (__atomic_load_n(&probe_finished, __ATOMIC_ACQUIRE))
            {
                /* Recheck after acquiring the producer's final publication. */
                if (iteration == __atomic_load_n(&probe_head, __ATOMIC_ACQUIRE))
                {
                    break;
                }
            }

            rt_thread_mdelay(1);
            continue;
        }

        struct proc_dentry *entry = probe_queue[iteration % PROC_PROBE_QUEUE_SIZE];

        /* Drop the tree reference, then the caller reference transferred to us. */
        probe_lock_take();
        proc_remove(entry);
        proc_release(entry);
        probe_lock_release();
        iteration++;
        __atomic_store_n(&probe_tail, iteration, __ATOMIC_RELEASE);
    }

    rt_sem_release(&probe_done);
}

/* Diagnostic only: run in a disposable guest. A failed public traversal can
 * remain stuck; keep its objects alive for GDB and reboot instead of retrying.
 */
static int procfs_smp_probe(int argc, char **argv)
{
    rt_thread_t producer = RT_NULL;
    rt_thread_t consumer = RT_NULL;
    unsigned int rounds = argc > 1 ? atoi(argv[1]) : 100000;
    unsigned int completed;

    if (rounds == 0 || rounds > 1000000)
    {
        return -RT_EINVAL;
    }

    if (__atomic_exchange_n(&probe_active, 1, __ATOMIC_ACQUIRE))
    {
        return -RT_EBUSY;
    }

    probe_head = probe_tail = probe_ready = probe_finished = probe_errors = 0;
    probe_rounds = rounds;
    probe_serialized = argc > 2 && atoi(argv[2]) != 0;
    rt_sem_init(&probe_done, "probdone", 0, RT_IPC_FLAG_FIFO);
    rt_mutex_init(&probe_lock, "problock", RT_IPC_FLAG_PRIO);
    probe_parent = proc_mkdir("__loongarch_smp_probe", RT_NULL);

    if (probe_parent == RT_NULL)
    {
        goto allocation_failed;
    }

    producer = rt_thread_create("probc", probe_create, RT_NULL, 4096, 20, 5);
    consumer = rt_thread_create("probr", probe_remove, RT_NULL, 4096, 20, 5);

    if (producer == RT_NULL || consumer == RT_NULL)
    {
        goto allocation_failed;
    }

    rt_thread_control(producer, RT_THREAD_CTRL_BIND_CPU, (void *)0);
    rt_thread_control(consumer, RT_THREAD_CTRL_BIND_CPU, (void *)1);
    rt_kprintf("PROCFS SMP PROBE: start rounds=%u serialized=%u cpus=0,1\n",
               rounds, probe_serialized);
    rt_thread_startup(producer);
    rt_thread_startup(consumer);

    for (completed = 0; completed < 2; completed++)
    {
        if (rt_sem_take(&probe_done, 30 * RT_TICK_PER_SECOND) != RT_EOK)
        {
            rt_kprintf("PROCFS SMP PROBE: TIMEOUT created=%u removed=%u ready=0x%x; reboot required\n",
                       __atomic_load_n(&probe_head, __ATOMIC_ACQUIRE),
                       __atomic_load_n(&probe_tail, __ATOMIC_ACQUIRE),
                       __atomic_load_n(&probe_ready, __ATOMIC_ACQUIRE));
            return -RT_ETIMEOUT;
        }
    }

    proc_remove(probe_parent);
    proc_release(probe_parent);
    rt_sem_detach(&probe_done);
    rt_mutex_detach(&probe_lock);
    rt_kprintf("PROCFS SMP PROBE: %s created=%u removed=%u errors=%u\n",
               probe_errors || probe_head != rounds || probe_tail != rounds ? "FAIL" : "PASS",
               probe_head, probe_tail, probe_errors);
    __atomic_store_n(&probe_active, 0, __ATOMIC_RELEASE);

    return probe_errors || probe_head != rounds || probe_tail != rounds ? -RT_ERROR : RT_EOK;

allocation_failed:
    if (producer != RT_NULL)
    {
        rt_thread_delete(producer);
    }

    if (consumer != RT_NULL)
    {
        rt_thread_delete(consumer);
    }

    if (probe_parent != RT_NULL)
    {
        proc_remove(probe_parent);
        proc_release(probe_parent);
    }

    rt_sem_detach(&probe_done);
    rt_mutex_detach(&probe_lock);
    __atomic_store_n(&probe_active, 0, __ATOMIC_RELEASE);
    return -RT_ENOMEM;
}

MSH_CMD_EXPORT(procfs_smp_probe, Diagnose concurrent procfs creation and removal on two CPUs);

#endif
