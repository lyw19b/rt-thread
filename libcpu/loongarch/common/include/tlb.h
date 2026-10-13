/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-11     LoongsonLab  the first version
 * 2025-03-20     LoongsonLab  page table init flags
 */

#ifndef __ASM_TLB_H__
#define __ASM_TLB_H__

#define PS_4K		0x0000000c
#define PS_8K		0x0000000d
#define PS_16K		0x0000000e
#define PS_64K		0x00000010
#define PS_1M		0x00000014
#define PS_2M		0x00000015
#define PS_16M		0x00000018
#define PS_64M		0x0000001a
#define PS_256M		0x0000001c
#define PS_1G		0x0000001e

#define PS_DEFAULT_SIZE		PS_4K

// CSR PWCL
#define MMU_PG_LEVEL_0_BASE_SHIFT      0
#define MMU_PG_LEVEL_0_WIDTH_SHIFT     5

#define MMU_PG_LEVEL_1_BASE_SHIFT      10
#define MMU_PG_LEVEL_1_WIDTH_SHIFT     15

#define MMU_PG_LEVEL_2_BASE_SHIFT      20
#define MMU_PG_LEVEL_2_WIDTH_SHIFT     25

#define MMU_PG_LEVEL_PTE_WIDTH_SHIFT   30

#define MMU_PG_LEVEL_PTE_WIDTH_64      0
#define MMU_PG_LEVEL_PTE_WIDTH_128     1
#define MMU_PG_LEVEL_PTE_WIDTH_256     2
#define MMU_PG_LEVEL_PTE_WIDTH_512     3

// CSR PWCH
#define MMU_PG_LEVEL_3_BASE_SHIFT      0
#define MMU_PG_LEVEL_3_WIDTH_SHIFT     6

#define MMU_STAT_EXCODE_PIL            0x1
#define MMU_STAT_EXCODE_PIS            0x2
#define MMU_STAT_EXCODE_PIF            0x3
#define MMU_STAT_EXCODE_PME            0x4

#endif /* __ASM_TLB_H__ */
