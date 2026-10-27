/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: bsp dma.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include "io.h"
#include "loongson_soc_info.h"
#include "irq.h"
#include "bsp_dma.h"

#define DMA_CHANNEL_MAX            8
#define DMA_IRQ_BASE_NUM           23
#define LS_DMA_IRQ_END(status_x)   (status_x & 0x2)
#define LS_DMA_IRQ_HALF(status_x)  (status_x & 0x4)
#define LS_DMA_IRQ_ERROR(status_x) (status_x & 0x8)

static inline int check_dma_channel_invaild(dma_transfer_info *info);
static int dma_transfer_info_invaild(dma_transfer_info *info);
static inline int get_dma_irq(dma_transfer_info *info);

static void dma_generic_irq_handle(int IRQn, void *param)
{
    unsigned char *dma_ISR;
    dma_transfer_info *info;
    int status;

    dma_ISR = (unsigned char *)LS_DMA_ISR;
    info = (dma_transfer_info *)param;
    status = readl(dma_ISR);
    status = LS_DMA_IRQ_STATUS(status, info->dma_channel);

    writel(LS_DMA_IRQ_STATUS_MASK(info->dma_channel), LS_DMA_IFCR);
    if (info->dma_callback)
    {
        if (LS_DMA_IRQ_ERROR(status))
        {
            info->dma_callback(info, DMA_TRANSFER_ERROR);
        }
        else if (LS_DMA_IRQ_END(status))
        {
            info->dma_callback(info, DMA_TRANSFER_END);
        }
        else if (LS_DMA_IRQ_HALF(status))
        {
            info->dma_callback(info, DMA_TRANSFER_HALF);
        }
    }
}

void dma_stop(dma_transfer_info *info)
{
    unsigned char *dma_IFCR;
    unsigned char *dma_channel_CCR;
    unsigned char channel;
    unsigned int value;

    if (check_dma_channel_invaild(info))
    {
        return;
    }

    channel = info->dma_channel;

    dma_IFCR = (unsigned char *)LS_DMA_IFCR;
    dma_channel_CCR = (unsigned char *)LS_DMA_CHANNEL_CCR(channel);

    // dma en to be 0 and disable irq bit 0 1 2 3
    value = readl(dma_channel_CCR);
    value |= 0xf;
    value ^= 0xf;
    writel(value, dma_channel_CCR);

    // clear dma irq by write 1
    value = LS_DMA_IRQ_STATUS_MASK(channel);
    writel(value, dma_IFCR);

    // if (info->dma_callback)
    // {
    //  lio_irq_disable(get_dma_irq(info));
    // }
}

int dma_config(dma_transfer_info *info)
{
    unsigned char *dma_channel_CCR;
    unsigned char *dma_channel_CNDTR;
    unsigned char *dma_channel_CPAR;
    unsigned char *dma_channel_CMAR;
    unsigned char channel;
    unsigned int value;

    if (check_dma_channel_invaild(info))
    {
        return 1;
    }

    if (dma_transfer_info_invaild(info))
    {
        return 1;
    }

    channel = info->dma_channel;
    dma_channel_CCR = (unsigned char *)LS_DMA_CHANNEL_CCR(channel);
    dma_channel_CNDTR = (unsigned char *)LS_DMA_CHANNEL_CNDTR(channel);
    dma_channel_CPAR = (unsigned char *)LS_DMA_CHANNEL_CPAR(channel);
    dma_channel_CMAR = (unsigned char *)LS_DMA_CHANNEL_CMAR(channel);

    // stop dma clear en and irq status
    dma_stop(info);

    writel(info->size, dma_channel_CNDTR);
    writel((unsigned int)LOONGARCH_ALIAS_PHYS(info->p_addr), dma_channel_CPAR);
    writel((unsigned int)LOONGARCH_ALIAS_PHYS(info->m_addr), dma_channel_CMAR);

    value = 0;
    value |= ((!!(info->dir)) << LS_DMA_DIR_OFFSET);
    value |= ((!!(info->circ)) << LS_DMA_CIRC_OFFSET);
    value |= ((!!(info->p_inc)) << LS_DMA_PINC_OFFSET);
    value |= ((!!(info->m_inc)) << LS_DMA_MINC_OFFSET);
    value |= ((info->p_size) << LS_DMA_P_SIZE_OFFSET);
    value |= ((info->m_size) << LS_DMA_M_SIZE_OFFSET);
    value |= ((info->dma_priority) << LS_DMA_PL_OFFSET);
    value |= ((!!(info->mem_2_mem)) << LS_DMA_MEM_2_MEM_OFFSET);
    if (info->dma_callback)
    {
        value |= (1 << LS_DMA_TEIE_OFFSET);
        value |= (1 << LS_DMA_TCIE_OFFSET);
    }
    writel(value, dma_channel_CCR);
    return 0;
}

int dma_start(dma_transfer_info *info)
{
    unsigned char *dma_channel_CCR;
    unsigned char channel;
    unsigned int value;

    if (check_dma_channel_invaild(info))
    {
        return 1;
    }

    if (dma_transfer_info_invaild(info))
    {
        return 1;
    }

    channel = info->dma_channel;
    dma_channel_CCR = (unsigned char *)LS_DMA_CHANNEL_CCR(channel);

    if (info->dma_callback)
    {
        rt_hw_interrupt_install(get_dma_irq(info), dma_generic_irq_handle, (void *)info, "apb-dma");
        rt_hw_interrupt_umask(get_dma_irq(info));
    }

    value = readl(dma_channel_CCR);
    value |= (1 << LS_DMA_EN_OFFSET);
    writel(value, dma_channel_CCR);
    return 0;
}

int dma_transfer(dma_transfer_info *info)
{
    if (dma_config(info))
    {
        return 1;
    }

    return dma_start(info);
}

int dma_check_transfer_end(dma_transfer_info *info)
{
    unsigned char *dma_ISR;
    unsigned char channel;
    unsigned int value;

    if (check_dma_channel_invaild(info))
    {
        return DMA_STATUS_FAILED;
    }

    channel = info->dma_channel;
    dma_ISR = (unsigned char *)LS_DMA_ISR;

    value = readl(dma_ISR);
    value = LS_DMA_IRQ_STATUS(value, channel);

    if (LS_DMA_IRQ_ERROR(value))
    {
        return DMA_STATUS_FAILED;
    }

    return LS_DMA_IRQ_END(value) ? DMA_STATUS_FINISH : DMA_STATUS_RUNNING;
}

void dma_transfer_info_init(dma_transfer_info *info)
{
    if (info)
    {
        rt_memset(info, 0, sizeof(*info));
    }
}

int dma_reconfig_m(dma_transfer_info *info)
{
    if (dma_transfer_info_invaild(info))
    {
        return 1;
    }

    unsigned char *dma_channel_CNDTR;
    unsigned char *dma_channel_CMAR;
    unsigned char channel;

    channel = info->dma_channel;
    dma_channel_CNDTR = (unsigned char *)LS_DMA_CHANNEL_CNDTR(channel);
    dma_channel_CMAR = (unsigned char *)LS_DMA_CHANNEL_CMAR(channel);

    writel(info->size, dma_channel_CNDTR);
    writel((unsigned int)LOONGARCH_ALIAS_PHYS(info->m_addr), dma_channel_CMAR);
    return 0;
}

static inline int check_dma_channel_invaild(dma_transfer_info *info)
{
    return (!info || info->dma_channel >= DMA_CHANNEL_MAX);
}

static int dma_transfer_info_invaild(dma_transfer_info *info)
{
    if (check_dma_channel_invaild(info))
    {
        return 1;
    }
    /* APB DMA has 32-bit bus addresses. Accept physical addresses or the
     * permanent uncached alias only; never truncate arbitrary pointers.
     * Callers retain ownership of channel serialization and uncached buffers. */
    if (!info->size || info->size > 65535 || LOONGARCH_ALIAS_PHYS(info->p_addr) > 0xffffffffUL ||
        LOONGARCH_ALIAS_PHYS(info->m_addr) > 0xffffffffUL ||
        (info->p_addr != LOONGARCH_ALIAS_PHYS(info->p_addr) &&
         info->p_addr != LOONGARCH_PHYS_UNCACHED(info->p_addr)) ||
        (info->m_addr != LOONGARCH_ALIAS_PHYS(info->m_addr) &&
         info->m_addr != LOONGARCH_PHYS_UNCACHED(info->m_addr)))
    {
        return 1;
    }

    if (info->p_size > DMA_SIZE_32)
    {
        return 1;
    }
    if (info->m_size > DMA_SIZE_32)
    {
        return 1;
    }
    if (info->dma_priority > DMA_PRIORITY_3)
    {
        return 1;
    }

    unsigned long p = LOONGARCH_ALIAS_PHYS(info->p_addr);
    unsigned long m = LOONGARCH_ALIAS_PHYS(info->m_addr);
    unsigned long pw = 1UL << info->p_size;
    unsigned long mw = 1UL << info->m_size;
    unsigned long pn = info->p_inc ? info->size : 1;
    unsigned long mn = info->m_inc ? info->size : 1;

    if ((p & (pw - 1)) || (m & (mw - 1)) || p + pn * pw - 1 > 0xffffffffUL ||
        m + mn * mw - 1 > 0xffffffffUL)
    {
        return 1;
    }

    return 0;
}

static inline int get_dma_irq(dma_transfer_info *info)
{
    return (DMA_IRQ_BASE_NUM + info->dma_channel);
}
