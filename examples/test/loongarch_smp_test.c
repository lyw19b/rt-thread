/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include <stdlib.h>
#ifdef RT_USING_SMP
#include <smp_call.h>
#ifdef RT_USING_SMART
#include <loongarch.h>
#endif
#endif

#ifdef RT_USING_SMP
static struct rt_semaphore completed;
static struct rt_spinlock counter_lock = RT_SPINLOCK_INIT;
static unsigned int active, ready, errors, atomic_count, locked_count;
static unsigned int seen[RT_CPUS_NR];
static unsigned int loops;
static unsigned int ipi_count[RT_CPUS_NR];

static void count_ipi(void *parameter)
{
    RT_UNUSED(parameter);
    __atomic_fetch_add(&ipi_count[rt_hw_cpu_id()], 1, __ATOMIC_RELAXED);
#ifdef RT_USING_SMART
    /* Verify the selected walker was initialized on every online CPU. */
#ifdef LOONGARCH_HARDWARE_PTW
    if (!(csr_read64(LOONGARCH_CSR_PWCTL1) & CSR_PWCTL1_PTW) ||
        !(read_cpucfg(2) & CPUCFG2_PTW))
#else
    if (csr_read64(LOONGARCH_CSR_PWCTL1) & CSR_PWCTL1_PTW)
#endif
        __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
#endif
}

static void test_migration(void)
{
    rt_thread_t self = rt_thread_self();
    unsigned int original = RT_SCHED_CTX(self).bind_cpu;
    unsigned int i;
    register double fp __asm__("$f24") = 123.75;

    for (i = 0; i < RT_CPUS_NR * 32; i++)
    {
        unsigned int cpu = i % RT_CPUS_NR;
        rt_thread_control(self, RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)cpu);
        __asm__ volatile("" : "+f"(fp));
        rt_thread_mdelay(1);
        __asm__ volatile("" : "+f"(fp));
        if (rt_hw_cpu_id() != cpu || fp != 123.75)
            errors++;
    }
    rt_thread_control(self, RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)original);
}

static void smp_worker(void *parameter)
{
    unsigned int cpu = (rt_ubase_t)parameter;
    unsigned int i;
    register double fp __asm__("$f24") = cpu + 0.25;

    __atomic_fetch_or(&ready, 1U << cpu, __ATOMIC_RELEASE);
    /* Pinned workers must all run before any can finish. A single running CPU
     * cannot satisfy this barrier when the configured CPU count exceeds one. */
    while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != (1U << RT_CPUS_NR) - 1)
        rt_thread_yield();
    for (i = 0; i < loops; i++)
    {
        rt_base_t level = rt_spin_lock_irqsave(&counter_lock);
        locked_count++;
        rt_spin_unlock_irqrestore(&counter_lock, level);
        __atomic_fetch_add(&atomic_count, 1, __ATOMIC_RELAXED);
        if (rt_hw_cpu_id() != cpu)
            __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
        seen[cpu]++;
        __asm__ volatile("" : "+f"(fp));
        if ((i & 255) == 0)
            rt_thread_mdelay(1);
        __asm__ volatile("" : "+f"(fp));
        if (fp != cpu + 0.25)
            __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
    }
    rt_sem_release(&completed);
}

static int smp_test(int argc, char **argv)
{
    rt_thread_t threads[RT_CPUS_NR] = {0};
    unsigned int cpu;
    unsigned int count = argc > 1 ? atoi(argv[1]) : 100000;
    rt_tick_t start;

    if (!count || count > 1000000)
        return -RT_EINVAL;
    if (__atomic_exchange_n(&active, 1, __ATOMIC_ACQUIRE))
        return -RT_EBUSY;
    loops = count;
    ready = errors = atomic_count = locked_count = 0;
    rt_memset(seen, 0, sizeof(seen));
    rt_sem_init(&completed, "smpdone", 0, RT_IPC_FLAG_FIFO);
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        threads[cpu] = rt_thread_create("smpwork", smp_worker, (void *)(rt_ubase_t)cpu,
                                        4096, 15, 2);
        if (!threads[cpu])
            goto failed_create;
        rt_thread_control(threads[cpu], RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)cpu);
    }
    start = rt_tick_get();
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        rt_thread_startup(threads[cpu]);
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (rt_sem_take(&completed, 120 * RT_TICK_PER_SECOND) != RT_EOK)
        {
            /* Workers may still be using the static IPC state. Keep it alive. */
            rt_kprintf("SMP TEST: FAIL (worker timeout, ready=0x%x)\n", ready);
            return -RT_ETIMEOUT;
        }
    }
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (seen[cpu] != loops || !rt_cpu_index(cpu)->tick)
            errors++;
        rt_kprintf("SMP CPU %u: iterations=%u tick=%lu\n", cpu, seen[cpu],
                   (rt_ubase_t)rt_cpu_index(cpu)->tick);
    }
    if (locked_count != loops * RT_CPUS_NR || atomic_count != locked_count)
        errors++;
    test_migration();
    rt_memset(ipi_count, 0, sizeof(ipi_count));
    for (cpu = 0; cpu < 100; cpu++)
        rt_smp_call_each_cpu(count_ipi, RT_NULL, SMP_CALL_WAIT_ALL);
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        if (ipi_count[cpu] != 100)
            errors++;
    rt_thread_mdelay(50);
    rt_sem_detach(&completed);
    rt_kprintf("SMP TEST: %s cpus=%u count=%u ticks=%lu errors=%u\n",
               errors ? "FAIL" : "PASS", RT_CPUS_NR, locked_count,
               rt_tick_get() - start, errors);
    __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    return errors ? -RT_ERROR : RT_EOK;

failed_create:
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        if (threads[cpu])
            rt_thread_delete(threads[cpu]);
    rt_sem_detach(&completed);
    __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    rt_kprintf("SMP TEST: FAIL (thread allocation)\n");
    return -RT_ENOMEM;
}
MSH_CMD_EXPORT(smp_test, Test CPU affinity migration IPI spinlocks atomics and FPU);
#endif

#ifdef RT_USING_SMP
static unsigned int reap_started;

static void reap_cleanup(rt_thread_t thread)
{
    RT_UNUSED(thread);
    rt_sem_release(&completed);
}

static void reap_worker(void *parameter)
{
    __atomic_fetch_add(&reap_started, 1, __ATOMIC_RELEASE);
    if (parameter)
    {
        /* Remote deletion must stop this context before its stack is freed. */
        for (;;)
            __asm__ volatile("" ::: "memory");
    }
}

static int smp_reap_test(int argc, char **argv)
{
    unsigned int rounds = argc > 1 ? atoi(argv[1]) : 1000;
    unsigned int round, cpu;
    unsigned int saved_cpu = RT_SCHED_CTX(rt_thread_self()).bind_cpu;

    if (!rounds || rounds > 100000)
        return -RT_EINVAL;
    if (__atomic_exchange_n(&active, 1, __ATOMIC_ACQUIRE))
        return -RT_EBUSY;
    rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_BIND_CPU, (void *)0);
    rt_thread_mdelay(1);
    rt_sem_init(&completed, "reapdone", 0, RT_IPC_FLAG_FIFO);
    for (round = 0; round < rounds; round++)
    {
        rt_thread_t threads[RT_CPUS_NR];
        unsigned int remote_delete = round & 1;

        reap_started = 0;
        for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        {
            threads[cpu] = rt_thread_create("reapwork", reap_worker,
                (void *)(rt_ubase_t)remote_delete, 4096, 25, 1);
            RT_ASSERT(threads[cpu]);
            threads[cpu]->cleanup = reap_cleanup;
            rt_thread_control(threads[cpu], RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)cpu);
            rt_thread_startup(threads[cpu]);
        }
        if (remote_delete)
        {
            while (__atomic_load_n(&reap_started, __ATOMIC_ACQUIRE) != RT_CPUS_NR)
                rt_thread_mdelay(1);
            for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
                rt_thread_delete(threads[cpu]);
        }
        for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        {
            void *reuse;
            if (rt_sem_take(&completed, 10 * RT_TICK_PER_SECOND) != RT_EOK)
            {
                rt_kprintf("SMP REAP TEST: FAIL (cleanup timeout)\n");
                return -RT_ETIMEOUT;
            }
            reuse = rt_malloc(4096);
            RT_ASSERT(reuse);
            rt_memset(reuse, round, 4096);
            rt_free(reuse);
        }
    }
    rt_sem_detach(&completed);
    rt_thread_control(rt_thread_self(), RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)saved_cpu);
    rt_kprintf("SMP REAP TEST: PASS cpus=%u rounds=%u threads=%u\n",
               RT_CPUS_NR, rounds, rounds * RT_CPUS_NR);
    __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    return RT_EOK;
}
MSH_CMD_EXPORT(smp_reap_test, Stress remote deletion thread exit and stack reclamation);
#endif

#if defined(RT_USING_SMP) && defined(RT_USING_SMART)
#include <mmu.h>
#include <ioremap.h>
#include <mm_page.h>

static struct
{
    rt_ubase_t *pages[RT_CPUS_NR][2];
    volatile rt_ubase_t *mapping[RT_CPUS_NR];
    unsigned int phase[RT_CPUS_NR];
    unsigned int slot[RT_CPUS_NR];
    unsigned int start;
    unsigned int ready;
    unsigned int rounds;
} tlb_test;

static void tlb_barrier(unsigned int cpu, unsigned int phase)
{
    unsigned int peer;

    __atomic_store_n(&tlb_test.phase[cpu], phase, __ATOMIC_RELEASE);
    for (peer = 0; peer < RT_CPUS_NR; peer++)
        while (__atomic_load_n(&tlb_test.phase[peer], __ATOMIC_ACQUIRE) < phase)
            loongarch_tlb_poll();
}

static void tlb_worker(void *parameter)
{
    unsigned int cpu = (rt_ubase_t)parameter;
    unsigned int round, peer;
    rt_base_t level;

    while (!__atomic_load_n(&tlb_test.start, __ATOMIC_ACQUIRE))
        rt_thread_mdelay(1);
    /* CPU 0 drives delayed wakeups. Keep its timer running until every worker
     * has left the sleep above, before entering the IRQ-masked test phase. */
    __atomic_fetch_or(&tlb_test.ready, 1U << cpu, __ATOMIC_RELEASE);
    while (__atomic_load_n(&tlb_test.ready, __ATOMIC_ACQUIRE) != (1U << RT_CPUS_NR) - 1)
        rt_thread_yield();
    level = rt_hw_local_irq_disable();
    /* Alternate concurrent updates with a single writer. Non-writers must
     * invalidate through shootdown, without a local map or context switch
     * accidentally hiding a stale translation. All CPUs keep IRQs masked. */
    for (round = 0; round < tlb_test.rounds; round++)
    {
        for (peer = 0; peer < RT_CPUS_NR; peer++)
        {
            rt_ubase_t expected = peer * 2 + tlb_test.slot[peer] + 1;
            if (*tlb_test.mapping[peer] != expected)
                __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
        }
        tlb_barrier(cpu, round * 2 + 1);
        if (!(round & 1) || cpu == (round / 2) % RT_CPUS_NR)
        {
            unsigned int slot = tlb_test.slot[cpu] ^ 1;
            rt_hw_mmu_unmap(&rt_kernel_space, (void *)tlb_test.mapping[cpu], ARCH_PAGE_SIZE);
            if (!rt_hw_mmu_map(&rt_kernel_space, (void *)tlb_test.mapping[cpu],
                               (void *)ARCH_PAGE_PA_MASK(tlb_test.pages[cpu][slot]),
                               ARCH_PAGE_SIZE, MMU_MAP_K_RWCB))
                __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
            tlb_test.slot[cpu] = slot;
        }
        tlb_barrier(cpu, round * 2 + 2);
    }
    rt_hw_local_irq_enable(level);
    rt_sem_release(&completed);
}

static int smp_tlb_test(int argc, char **argv)
{
    rt_thread_t threads[RT_CPUS_NR] = {0};
    unsigned int cpu, slot;
    unsigned int rounds = argc > 1 ? atoi(argv[1]) : 1000;
    int result = -RT_ENOMEM;

    if (!rounds || rounds > 100000)
        return -RT_EINVAL;
    if (__atomic_exchange_n(&active, 1, __ATOMIC_ACQUIRE))
        return -RT_EBUSY;
    rt_memset(&tlb_test, 0, sizeof(tlb_test));
    errors = 0;
    tlb_test.rounds = rounds;
    rt_sem_init(&completed, "tlbdone", 0, RT_IPC_FLAG_FIFO);
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        for (slot = 0; slot < 2; slot++)
        {
            tlb_test.pages[cpu][slot] = rt_pages_alloc(0);
            if (!tlb_test.pages[cpu][slot])
                goto cleanup;
            *tlb_test.pages[cpu][slot] = cpu * 2 + slot + 1;
        }
        tlb_test.mapping[cpu] = rt_ioremap_cached(
            (void *)ARCH_PAGE_PA_MASK(tlb_test.pages[cpu][0]), ARCH_PAGE_SIZE);
        if (!tlb_test.mapping[cpu])
            goto cleanup;
        threads[cpu] = rt_thread_create("tlbwork", tlb_worker, (void *)(rt_ubase_t)cpu,
                                        4096, 15, 2);
        if (!threads[cpu])
            goto cleanup;
        rt_thread_control(threads[cpu], RT_THREAD_CTRL_BIND_CPU, (void *)(rt_ubase_t)cpu);
    }
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        rt_thread_startup(threads[cpu]);
    __atomic_store_n(&tlb_test.start, 1, __ATOMIC_RELEASE);
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (rt_sem_take(&completed, 120 * RT_TICK_PER_SECOND) != RT_EOK)
        {
            rt_kprintf("SMP TLB TEST: FAIL (worker timeout)\n");
            return -RT_ETIMEOUT;
        }
    }
    rt_thread_mdelay(50);
    rt_memset(threads, 0, sizeof(threads));
    result = errors ? -RT_ERROR : RT_EOK;
cleanup:
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (threads[cpu])
            rt_thread_delete(threads[cpu]);
        if (tlb_test.mapping[cpu])
            rt_iounmap(tlb_test.mapping[cpu]);
        for (slot = 0; slot < 2; slot++)
            if (tlb_test.pages[cpu][slot])
                rt_pages_free(tlb_test.pages[cpu][slot], 0);
    }
    rt_sem_detach(&completed);
    rt_kprintf("SMP TLB TEST: %s cpus=%u rounds=%u errors=%u\n",
               result ? "FAIL" : "PASS", RT_CPUS_NR, rounds, errors);
    __atomic_store_n(&active, 0, __ATOMIC_RELEASE);
    return result;
}
MSH_CMD_EXPORT(smp_tlb_test, Stress concurrent TLB shootdown and page table reclamation);
#endif
