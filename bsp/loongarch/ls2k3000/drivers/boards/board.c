/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Initialize only memory reserved by the EFI loader.
 */

#include <rthw.h>
#include "board.h"
#include "loongarch.h"
#include "timer.h"

#ifdef RT_USING_SMART
#include <mm_aspace.h>
#include <mm_page.h>
#include "mmu.h"
#include "lwp_arch.h"
extern rt_ubase_t MMUTable[];
#endif

extern unsigned long fw_arg0;
extern unsigned long fw_arg1;
const struct ls2k3000_boot_info *ls2k3000_boot;

void platform_irq_init(void)
{
    /* First-stage drivers poll. Do not inherit firmware I/O interrupt routes. */
    clear_csr_ecfg(0x1fff & ~ECFGF_IPI);
}

void rt_hw_timer_init(void)
{
    rt_uint64_t frequency = read_cpucfg(LOONGARCH_CPUCFG4);
    rt_uint32_t ratio = read_cpucfg(LOONGARCH_CPUCFG5);
    rt_uint64_t interval;

    RT_ASSERT(frequency && (ratio & 0xffff) && (ratio >> 16));
    frequency = frequency * (ratio & 0xffff) / (ratio >> 16);
    interval = frequency / RT_TICK_PER_SECOND;
    clear_csr_ecfg(0x1fff & ~ECFGF_IPI);
    csr_write64((interval & CSR_TCFG_VAL) | CSR_TCFG_PERIOD | CSR_TCFG_EN,
                LOONGARCH_CSR_TCFG);
    set_csr_ecfg(ECFGF_TIMER);
}

void rt_hw_board_init(void)
{
    const struct ls2k3000_boot_info *info =
        (const void *)LOONGARCH_PHYS_CACHED(LS2K3000_HANDOFF_PHYS);

    if (fw_arg0 != LS2K3000_BOOT_MAGIC || fw_arg1 != LS2K3000_HANDOFF_PHYS ||
        ls2k3000_boot_validate(info))
    {
        ls2k3000_early_puts("LS2K3000: invalid EFI handoff; kernel stopped\r\n");

        for (;;)
        {
            __asm__ volatile("idle 0");
        }
    }

    ls2k3000_boot = info;

#ifdef RT_USING_SMART
    rt_region_t pages = {
        .start = (rt_size_t)RT_HW_PAGE_START,
        .end = (rt_size_t)RT_HW_PAGE_END,
        .name = "EFI reserved DDR",
    };

    rt_kmem_pvoff_set(-LOONGARCH_CACHED_BASE);
    rt_hw_mmu_map_init(&rt_kernel_space, (void *)0xffffffc010000000UL,
                       16UL * 1024 * 1024, MMUTable, PV_OFFSET);
    rt_page_init(pages);
    rt_hw_mmu_setup(&rt_kernel_space, RT_NULL, 0);
#endif

    RT_ASSERT((rt_ubase_t)RT_HW_HEAP_BEGIN < (rt_ubase_t)RT_HW_HEAP_END);
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);

#ifdef BSP_USING_DMA_POOL
    int dma_result = ls2k3000_dma_init();
#endif

    rt_hw_interrupt_init();
    ls2k3000_uart_init();
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#ifdef BSP_USING_DMA_POOL
    if (dma_result != RT_EOK)
    {
        rt_kprintf("DMA pool unavailable: unsupported cache/coherency policy (%d)\n", dma_result);
    }
#endif
    rt_hw_timer_init();
    rt_components_board_init();
}

void ls2k3000_boot_status(void)
{
    rt_kprintf("UEFI handoff: v%u kernel=%lx size=%lu reserved=%lu memory=%u pci=%u\n",
               ls2k3000_boot->version, (unsigned long)ls2k3000_boot->kernel_base,
               (unsigned long)ls2k3000_boot->kernel_bytes,
               (unsigned long)ls2k3000_boot->reserved_bytes,
               ls2k3000_boot->memory_count, ls2k3000_boot->pci_count);
    rt_kprintf("GOP raw: status=%lx handles=%u base=%lx bytes=%lu %ux%u stride=%u format=%u\n",
               (unsigned long)ls2k3000_boot->gop_status, ls2k3000_boot->gop_handles,
               (unsigned long)ls2k3000_boot->gop_raw.base,
               (unsigned long)ls2k3000_boot->gop_raw.bytes,
               ls2k3000_boot->gop_raw.width, ls2k3000_boot->gop_raw.height,
               ls2k3000_boot->gop_raw.stride, ls2k3000_boot->gop_raw.format);
    rt_kprintf("GOP masks: red=%x green=%x blue=%x reserved=%x\n",
               ls2k3000_boot->gop_masks[0], ls2k3000_boot->gop_masks[1],
               ls2k3000_boot->gop_masks[2], ls2k3000_boot->gop_masks[3]);
    rt_kprintf("RESET: address=%lx bits=%u value=%u\n",
               (unsigned long)ls2k3000_boot->reset_address,
               ls2k3000_boot->reset_bits, ls2k3000_boot->reset_value);
}

MSH_CMD_EXPORT(ls2k3000_boot_status, Show validated EFI resources);
