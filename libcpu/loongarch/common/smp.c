/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rthw.h>
#include <rtthread.h>
#include "loongarch.h"
#include "timer.h"
#include "kernel_map.h"
#ifdef RT_USING_SMART
#include "mmu.h"
#endif

#ifdef RT_USING_SMP
#include <smp_call.h>

#define IPI_STATUS      0x1000
#define IPI_ENABLE      0x1004
#define IPI_CLEAR       0x100c
#define IPI_SEND        0x1040
#define IPI_MAIL_SEND   0x1048
#define IPI_SEND_WAIT   (1UL << 31)
#define IPI_TLB         3

static unsigned int online_mask;
#if RT_CPUS_NR > 1
rt_align(16) rt_uint8_t loongarch_secondary_stacks[RT_CPUS_NR - 1][__STACKSIZE__];
#endif

/* IOCSR mailbox and IPI transport shared by QEMU and LS2K. */
static unsigned int ipi_read(unsigned int reg)
{
    return iocsr_read32(reg);
}

static void ipi_write(unsigned int reg, unsigned int value)
{
    iocsr_write32(value, reg);
    rt_hw_dmb();
}

static void ipi_send(unsigned int cpu, unsigned int vector)
{
    iocsr_write32(IPI_SEND_WAIT | (cpu << 16) | vector, IPI_SEND);
}

/**
 * @brief    Publish a secondary CPU entry address through IOCSR mailbox 0.
 *
 * @param    cpu is the destination hardware CPU ID.
 *
 * @param    entry is the physical entry address expected by the firmware.
 */
static void mailbox_write(unsigned int cpu, rt_ubase_t entry)
{
    rt_uint64_t target = IPI_SEND_WAIT | (cpu << 16);

    /* Mailbox 0 contains a 64-bit entry, written through two 32-bit sends. */
    iocsr_write64(target | (1U << 2) | (entry & 0xffffffff00000000UL), IPI_MAIL_SEND);
    iocsr_write64(target | (entry << 32), IPI_MAIL_SEND);
}

/**
 * @brief    Read the mask of CPUs available for inter-processor requests.
 *
 * @note     Acquire ordering pairs with each CPU publishing its initialized state.
 *
 * @return   A bit mask indexed by hardware CPU ID.
 */
unsigned int loongarch_cpu_online_mask(void)
{
    return __atomic_load_n(&online_mask, __ATOMIC_ACQUIRE);
}

#ifdef RT_USING_SMART
/* Each sender owns one row. IRQ masking prevents two requests from the same
 * CPU overlapping. Receivers never acquire a lock, including while polling
 * from a spin wait with local interrupts disabled. */
static unsigned int tlb_request[RT_CPUS_NR][RT_CPUS_NR];
static unsigned int tlb_ack[RT_CPUS_NR][RT_CPUS_NR];
static unsigned int tlb_sequence[RT_CPUS_NR];

/**
 * @brief    Acknowledge pending TLB invalidations on the local CPU.
 *
 * @note     Preserves the local IRQ state and does not acquire locks. Spin waits
 *           call this even with IRQs masked, so a remote page-table owner can progress.
 */
void loongarch_tlb_poll(void)
{
    rt_base_t level = rt_hw_local_irq_disable();
    unsigned int cpu = rt_hw_cpu_id();
    unsigned int sender;

    for (sender = 0; sender < RT_CPUS_NR; sender++)
    {
        unsigned int request = __atomic_load_n(&tlb_request[sender][cpu], __ATOMIC_ACQUIRE);
        if (request != __atomic_load_n(&tlb_ack[sender][cpu], __ATOMIC_RELAXED))
        {
            rt_hw_tlb_invalidate_all_local();
            __atomic_store_n(&tlb_ack[sender][cpu], request, __ATOMIC_RELEASE);
        }
    }
    rt_hw_local_irq_enable(level);
}

/**
 * @brief    Invalidate local and remote TLBs before returning to the caller.
 *
 * @note     Masks local IRQs to serialize requests from this CPU. While waiting
 *           for every online peer to acknowledge, services incoming requests without
 *           acquiring another page-table lock. CPU hot-unplug is not supported.
 */
void loongarch_tlb_flush_all(void)
{
    rt_base_t level = rt_hw_local_irq_disable();
    unsigned int self = rt_hw_cpu_id();
    unsigned int mask = loongarch_cpu_online_mask() & ~(1U << self);
    unsigned int sequence = ++tlb_sequence[self];
    unsigned int cpu;

    rt_hw_dmb();
    rt_hw_tlb_invalidate_all_local();
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (mask & (1U << cpu))
        {
            __atomic_store_n(&tlb_request[self][cpu], sequence, __ATOMIC_RELEASE);
            ipi_send(cpu, IPI_TLB);
        }
    }
    
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        if (mask & (1U << cpu))
        {
            while (__atomic_load_n(&tlb_ack[self][cpu], __ATOMIC_ACQUIRE) != sequence)
                loongarch_tlb_poll();
        }
    }
    rt_hw_local_irq_enable(level);
}
#else
void loongarch_tlb_poll(void)
{
}
#endif

/**
 * @brief    Initialize an unlocked hardware spinlock.
 *
 * @param    lock is the spinlock to initialize before it is shared.
 */
void rt_hw_spin_lock_init(rt_hw_spinlock_t *lock)
{
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELAXED);
}

/**
 * @brief    Acquire a hardware spinlock with acquire memory ordering.
 *
 * @note     The caller manages scheduler and IRQ exclusion. A contended wait
 *           services TLB requests so it cannot block a remote shootdown behind the lock.
 *
 * @param    lock is the spinlock to acquire.
 */
void rt_hw_spin_lock(rt_hw_spinlock_t *lock)
{
    while (__atomic_exchange_n(&lock->value, 1, __ATOMIC_ACQUIRE))
    {
        do
        {
            /* A page-table owner may be waiting for our TLB acknowledgement. */
            loongarch_tlb_poll();
        } while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED));
    }
}

/**
 * @brief    Release a hardware spinlock with release memory ordering.
 *
 * @param    lock is the spinlock held by the caller.
 */
void rt_hw_spin_unlock(rt_hw_spinlock_t *lock)
{
    __atomic_store_n(&lock->value, 0, __ATOMIC_RELEASE);
}

/**
 * @brief    Send an RT-Thread IPI to the selected online CPUs.
 *
 * @note     Offline CPUs are ignored. A memory barrier publishes preceding
 *           shared-memory updates before the notification is sent.
 *
 * @param    vector is the IPI bit index in the range [0, RT_MAX_IPI).
 *
 * @param    cpu_mask is the destination mask indexed by hardware CPU ID.
 */
void rt_hw_ipi_send(int vector, unsigned int cpu_mask)
{
    unsigned int cpu;

    RT_ASSERT(vector >= 0 && vector < RT_MAX_IPI);
    cpu_mask &= loongarch_cpu_online_mask();
    rt_hw_dmb();
    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
        if (cpu_mask & (1U << cpu))
            ipi_send(cpu, vector);
}

/**
 * @brief    Acknowledge and dispatch pending local IOCSR interrupts.
 *
 * @note     Runs in interrupt context. TLB requests are serviced before scheduler
 *           and SMP callbacks. A stop request parks this CPU with local IRQs masked.
 */
void loongarch_ipi_handler(void)
{
    unsigned int pending = ipi_read(IPI_STATUS);

    ipi_write(IPI_CLEAR, pending);
    loongarch_tlb_poll();
    if (pending & (1U << RT_STOP_IPI))
    {
        rt_hw_local_irq_disable();
        while (1)
            __asm__ volatile("idle 0");
    }
    if (pending & (1U << RT_SCHEDULE_IPI))
        rt_scheduler_ipi_handler(RT_SCHEDULE_IPI, RT_NULL);
    if (pending & (1U << RT_SMP_CALL_IPI))
        rt_smp_call_ipi_handler(RT_SMP_CALL_IPI, RT_NULL);
}

/**
 * @brief    Start the configured secondary CPUs from CPU 0.
 *
 * @note     Firmware must wait for a physical entry address in IOCSR mailbox 0.
 *           CPU IDs must be contiguous from zero. Startup stops with an assertion if
 *           a CPU does not publish its online bit within five seconds.
 */
void rt_hw_secondary_cpu_up(void)
{
    extern void loongarch_secondary_entry(void);
    unsigned int cpu;
    rt_uint64_t frequency = (rt_uint64_t)read_cpucfg(LOONGARCH_CPUCFG4);
    unsigned int ratio = read_cpucfg(LOONGARCH_CPUCFG5);

    frequency = frequency * (ratio & 0xffff) / (ratio >> 16);
    RT_ASSERT(rt_hw_cpu_id() == 0);
    ipi_write(IPI_CLEAR, ~0U);
    ipi_write(IPI_ENABLE, ~0U);
    __atomic_store_n(&online_mask, 1, __ATOMIC_RELEASE);
    for (cpu = 1; cpu < RT_CPUS_NR; cpu++)
    {
        rt_ubase_t start = drdtime();

        mailbox_write(cpu, LOONGARCH_ALIAS_PHYS(loongarch_secondary_entry));
        ipi_send(cpu, RT_SCHEDULE_IPI);
        while (!(loongarch_cpu_online_mask() & (1U << cpu)))
        {
            if (drdtime() - start > frequency * 5)
            {
                rt_kprintf("SMP: CPU %u did not start; check firmware and CPU count\n", cpu);
                RT_ASSERT(0);
                while (1) {}
            }
        }
    }
    rt_kprintf("SMP: %u CPUs online (mask 0x%x)\n", RT_CPUS_NR, loongarch_cpu_online_mask());
}

/**
 * @brief    Initialize a secondary CPU and enter its scheduler.
 *
 * @note     Entered with local IRQs masked and a private boot stack. Publishes
 *           CPU-local exception/MMU readiness, then waits for the boot CPU startup
 *           lock before enabling its timer and starting scheduling. Does not return.
 */
void loongarch_secondary_start(void)
{
    init_cpu_early();
#ifdef RT_USING_SMART
    csr_write64(ARCH_PAGE_PA_MASK(rt_kernel_space.page_table), LOONGARCH_CSR_PGDH);
    rt_hw_aspace_switch(&rt_kernel_space);
#endif
    ipi_write(IPI_CLEAR, ~0U);
    ipi_write(IPI_ENABLE, ~0U);
    __atomic_fetch_or(&online_mask, 1U << rt_hw_cpu_id(), __ATOMIC_RELEASE);
    /* Startup holds this lock until the global scheduler and idle threads exist. */
    rt_hw_spin_lock(&_cpus_lock);
    rt_hw_timer_init();
    rt_system_scheduler_start();
}

/**
 * @brief    Wait for an interrupt on an idle secondary CPU.
 */
void rt_hw_secondary_cpu_idle_exec(void)
{
    __asm__ volatile("idle 0");
}
#endif
