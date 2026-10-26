/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    verify real ALE returns and rejected ranges
 */
#include <rtthread.h>
#include <inst.h>
#include <stack.h>
#include <kernel_map.h>

#ifdef BSP_USING_SELFTEST
#include "loongarch_ale_cases.h"

static rt_align(4096) unsigned char ale_buffer[8192];
static const rt_uint32_t ale_load = 0x28c00004; /* ld.d a0, zero, 0 */
static const rt_uint32_t ale_store = 0x29c00004; /* st.d a0, zero, 0 */
static const rt_uint32_t ale_atomic = 0x20000004; /* ll.w is never byte-emulated */

static long ale_test(void)
{
    struct pt_regs regs, before;
    rt_ubase_t invalid[] = { 1, LOONGARCH_UNCACHED_BASE + 0x16100001,
                            ~(rt_ubase_t)0 - 2 };
    unsigned int i, round;
    int result;
    rt_ubase_t recovered = loongarch_ale_count(RT_FALSE);

    if (!loongarch_unaligned_access_ok((rt_ubase_t)ale_buffer, sizeof(ale_buffer),
                                      RT_TRUE, RT_FALSE))
    {
        rt_kprintf("ALE TEST: unsupported BSP RAM policy\n");
        return -1;
    }

    for (round = 0; round < 100; round++)
    {
        result = ale_run_cases(ale_buffer);

        if (!result)
        {
            result = ale_run_cases(ale_buffer + 4096 - 16);
        }

        if (result)
        {
            rt_kprintf("ALE TEST: FAIL instruction=%d round=%u\n", result, round);
            return -1;
        }
    }

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]) + 3; i++)
    {
        rt_memset(&regs, 0, sizeof(regs));
        regs.r_era = (rt_ubase_t)&ale_load;
        regs.r_bvaddr = (rt_ubase_t)ale_buffer + 1;
        regs.r_a0 = 0xfedcba9876543210UL;

        if (i < sizeof(invalid) / sizeof(invalid[0]))
        {
            regs.r_bvaddr = invalid[i];
        }
        else if (i == 3)
        {
            regs.r_era = (rt_ubase_t)&ale_atomic;
        }
        else if (i == 4)
        {
            regs.r_era = (rt_ubase_t)&ale_store;
            regs.r_bvaddr = (rt_ubase_t)&ale_load + 1;
        }
        else
        {
            regs.r_era = 1;
        }

        before = regs;

        if (loongarch_try_emulate_ale(&regs) || rt_memcmp(&regs, &before, sizeof(regs)))
        {
            rt_kprintf("ALE TEST: FAIL rejection=%u\n", i);
            return -1;
        }
    }

    rt_kprintf("ALE TEST: PASS 28 opcodes, 100 rounds, cross-page, r0, rejected ranges, recovered=%lu\n",
               loongarch_ale_count(RT_FALSE) - recovered);
    return 0;
}
MSH_CMD_EXPORT(ale_test, Execute scalar unaligned instructions and check exception return);

static long ale_stats(void)
{
    rt_kprintf("ALE recovered: kernel=%lu user=%lu\n",
               loongarch_ale_count(RT_FALSE), loongarch_ale_count(RT_TRUE));
    return 0;
}
MSH_CMD_EXPORT(ale_stats, Show successful alignment exception recovery counts);
#endif
