/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 */

#ifndef __ASM_CPUPORT_H__
#define __ASM_CPUPORT_H__

#include <rtconfig.h>

#ifdef RT_USING_SMP
typedef struct
{
    unsigned int value;
} rt_hw_spinlock_t;
#endif

#include <rttypes.h>
#include <rtcompiler.h>

#ifdef RT_USING_SMP
void loongarch_ipi_handler(void);
void loongarch_tlb_poll(void);
unsigned int loongarch_cpu_online_mask(void);
#endif

void init_cpu_early(void);

void trap_init(void);
void init_smp(void);
void init_mmu(void);

rt_inline rt_ubase_t drdtime(void)
{
	rt_ubase_t val = 0;

	__asm__ __volatile__(
		"rdtime.d %0, $zero\n\t"
		: "=r"(val)
		:
		);
	return val;
}

rt_inline void rt_hw_dsb(void)
{
    __asm__ volatile("dbar 0":::"memory");
}

rt_inline void rt_hw_dmb(void)
{
    __asm__ volatile("dbar 0":::"memory");
}

rt_inline void rt_hw_isb(void)
{
    __asm__ volatile("ibar 0":::"memory");
}


#endif /* __ASM_CPUPORT_H__ */
