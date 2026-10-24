/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-10-24     OpenLANPU    Verify kernel mapping modes and alias reclamation
 */

#include <rtthread.h>
#include <rthw.h>
#include <stdlib.h>

#ifdef RT_USING_SMART
#include <ioremap.h>
#include "mmu.h"
#include "loongarch.h"
#ifdef RT_USING_SMP
#include <smp_call.h>
#endif

struct mapping_cpu_state
{
    rt_ubase_t dmw[4];
    rt_ubase_t pgdh;
    rt_ubase_t crmd;
};

static struct mapping_cpu_state mapping_cpus[RT_CPUS_NR];
static unsigned int mapping_busy;
/* Exercise the uncached-only DMA window even without a hardware SD controller. */
static rt_uint8_t mapping_dma[8192] __attribute__((section(".sdio_dma"), aligned(4096)));
extern char __text_start[], __vectors_start[], __dma_start[], __dma_end[];

static void mapping_cpu_read(void *parameter)
{
    struct mapping_cpu_state *state = &mapping_cpus[rt_hw_cpu_id()];

    RT_UNUSED(parameter);
    state->dmw[0] = csr_read64(LOONGARCH_CSR_DMWIN0);
    state->dmw[1] = csr_read64(LOONGARCH_CSR_DMWIN1);
    state->dmw[2] = csr_read64(LOONGARCH_CSR_DMWIN2);
    state->dmw[3] = csr_read64(LOONGARCH_CSR_DMWIN3);
    state->pgdh = csr_read64(LOONGARCH_CSR_PGDH);
    state->crmd = csr_read64(LOONGARCH_CSR_CRMD);
}

#ifdef KMD_USE_PAGE
static rt_ubase_t mapping_leaf(rt_ubase_t va)
{
    rt_ubase_t *table = rt_kernel_space.page_table;
    unsigned int shift;

    for (shift = 30; shift > 12; shift -= 9)
    {
        rt_ubase_t entry = table[(va >> shift) & 511];

        if (!entry || (entry & (1UL << MMU_PAGE_HUGE_SHIFT)))
        {
            return 0;
        }

        table = (void *)ARCH_PAGE_PA2VA(entry);
    }

    return table[(va >> 12) & 511];
}
#endif

static int kernel_map_test(int argc, char **argv)
{
    unsigned int rounds = argc > 1 ? atoi(argv[1]) : 32;
    unsigned int cpu, round, errors = 0;
    volatile rt_uint8_t *dma = (void *)LOONGARCH_PHYS_UNCACHED(mapping_dma);
    const rt_size_t words = (8UL * 1024 * 1024) / sizeof(rt_ubase_t);
    rt_ubase_t *memory = RT_NULL;
    rt_ubase_t *alias = RT_NULL;

    if (!rounds || rounds > 512)
    {
        return -RT_EINVAL;
    }

    if (__atomic_exchange_n(&mapping_busy, 1, __ATOMIC_ACQUIRE))
    {
        return -RT_EBUSY;
    }

#ifdef RT_USING_SMP
    rt_smp_call_each_cpu(mapping_cpu_read, RT_NULL, SMP_CALL_WAIT_ALL);
#else
    mapping_cpu_read(RT_NULL);
#endif

    for (cpu = 0; cpu < RT_CPUS_NR; cpu++)
    {
        struct mapping_cpu_state *state = &mapping_cpus[cpu];

        errors += (state->crmd & 0x18) != 0x10;
        errors += state->pgdh != ARCH_PAGE_PA_MASK(rt_kernel_space.page_table);
#ifdef KMD_USE_PAGE
        errors += (state->dmw[0] | state->dmw[1] | state->dmw[2] | state->dmw[3]) != 0;
#else
        errors += !(state->dmw[0] & 1) || !(state->dmw[1] & 1);
#endif
        rt_kprintf("KMD CPU %u: dmw=%lx/%lx/%lx/%lx pgdh=%lx crmd=%lx\n",
                   cpu, state->dmw[0], state->dmw[1], state->dmw[2], state->dmw[3],
                   state->pgdh, state->crmd);
    }

#ifdef KMD_USE_PAGE
    {
        rt_ubase_t text = mapping_leaf((rt_ubase_t)__text_start);
        rt_ubase_t data = mapping_leaf((rt_ubase_t)mapping_cpus);
        rt_ubase_t vectors = mapping_leaf((rt_ubase_t)__vectors_start);
        rt_ubase_t device = mapping_leaf(LOONGARCH_UNCACHED_BASE + 0x16100000);
        rt_ubase_t va;

        errors += !(text & MMU_PAGE_VALID) ||
                  !!(text & (MMU_PAGE_WRITE | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC | MMU_PAGE_USER));
        errors += !(data & MMU_PAGE_NO_EXEC) || !(data & MMU_PAGE_DIRTY);
        errors += !(vectors & MMU_PAGE_VALID) ||
                  !!(vectors & (MMU_PAGE_WRITE | MMU_PAGE_DIRTY | MMU_PAGE_NO_EXEC));
        errors += !(device & MMU_PAGE_NO_EXEC) || !!(device & (MMU_CACHE_CC | MMU_PAGE_USER));
        errors += mapping_leaf(LOONGARCH_CACHED_BASE + BSP_RAM_END) != 0;
        errors += rt_hw_mmu_v2p(&rt_kernel_space, (void *)0x9000000000200000UL) != ARCH_MAP_FAILED;
        errors += rt_hw_mmu_v2p(&rt_kernel_space, (void *)0x8000000016100000UL) != ARCH_MAP_FAILED;
        errors += (rt_ubase_t)rt_kernel_space.start + rt_kernel_space.size > LOONGARCH_CACHED_BASE;

        for (va = (rt_ubase_t)__dma_start; va < (rt_ubase_t)__dma_end; va += ARCH_PAGE_SIZE)
        {
            rt_ubase_t uncached = LOONGARCH_PHYS_UNCACHED(va);
            rt_ubase_t leaf = mapping_leaf(uncached);

            errors += mapping_leaf(va) != 0;
            errors += !(leaf & MMU_PAGE_VALID) || !!(leaf & MMU_CACHE_CC);
            errors += (rt_ubase_t)rt_hw_mmu_v2p(&rt_kernel_space, (void *)uncached) !=
                      LOONGARCH_ALIAS_PHYS(va);
        }
    }
#endif

    for (round = 0; round < sizeof(mapping_dma); round++)
    {
        dma[round] = (rt_uint8_t)(round ^ 0x5a);
    }

    for (round = 0; round < sizeof(mapping_dma); round++)
    {
        errors += dma[round] != (rt_uint8_t)(round ^ 0x5a);
    }

    memory = rt_pages_alloc(11);
    if (!memory)
    {
        errors++;
        goto done;
    }

    for (round = 0; round < rounds; round++)
    {
        rt_size_t word;

        alias = rt_ioremap_cached((void *)ARCH_PAGE_PA_MASK(memory), words * sizeof(*memory));
        if (!alias)
        {
            errors++;
            break;
        }

        for (word = 0; word < words; word++)
        {
            memory[word] = (word * 0x9e3779b1UL) ^ round;
        }

        for (word = 0; word < words; word++)
        {
            if (alias[word] != ((word * 0x9e3779b1UL) ^ round))
            {
                errors++;
                break;
            }

            alias[word] = ~alias[word];
        }

        for (word = 0; word < words; word++)
        {
            if (memory[word] != ~((word * 0x9e3779b1UL) ^ round))
            {
                errors++;
                break;
            }
        }

        errors += rt_hw_mmu_v2p(&rt_kernel_space, (char *)alias + 4097) !=
                  (void *)(ARCH_PAGE_PA_MASK(memory) + 4097);
        rt_iounmap(alias);
        errors += rt_hw_mmu_v2p(&rt_kernel_space, alias) != ARCH_MAP_FAILED;
        alias = RT_NULL;
    }

done:
    if (memory)
    {
        rt_pages_free(memory, 11);
    }

    rt_kprintf("KERNEL MAP TEST: %s mode=%s cpus=%u rounds=%u MiB=%u errors=%u\n",
               errors ? "FAIL" : "PASS",
#ifdef KMD_USE_PAGE
               "PAGE",
#else
               "DMW",
#endif
               RT_CPUS_NR, rounds, rounds * 8, errors);
    __atomic_store_n(&mapping_busy, 0, __ATOMIC_RELEASE);
    return errors ? -RT_ERROR : RT_EOK;
}
MSH_CMD_EXPORT(kernel_map_test, Verify mapping CSRs permissions and 8 MiB remap roundtrips);

#ifdef KMD_USE_PAGE
/* Destructive diagnostics: each command must run in a fresh test guest. */
static int kernel_map_fault(int argc, char **argv)
{
    if (argc != 2)
    {
        return -RT_EINVAL;
    }

    rt_kprintf("KERNEL MAP FAULT: armed %s\n", argv[1]);
    if (!rt_strcmp(argv[1], "ro"))
    {
        *(volatile rt_ubase_t *)__text_start = 0;
    }
    else if (!rt_strcmp(argv[1], "nx"))
    {
        ((void (*)(void))mapping_cpus)();
    }
    else if (!rt_strcmp(argv[1], "dmw"))
    {
        *(volatile rt_ubase_t *)0x9000000000200000UL = 0;
    }
    else if (!rt_strcmp(argv[1], "dma"))
    {
        *(volatile rt_uint8_t *)mapping_dma = 0;
    }
    else if (!rt_strcmp(argv[1], "low"))
    {
        *(volatile rt_ubase_t *)0x200000UL = 0;
    }
    else
    {
        return -RT_EINVAL;
    }

    rt_kprintf("KERNEL MAP FAULT: FAIL access was permitted\n");
    return -RT_ERROR;
}
MSH_CMD_EXPORT(kernel_map_fault, Destructive RO NX DMW DMA alias and low address diagnostics);
#endif
#endif /* RT_USING_SMART */
