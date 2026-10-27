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
 *   LS2K0300 development BSP: bsp dma map.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rthw.h>
#include "io.h"
#include "loongson_soc_info.h"
#include "bsp_dma_map.h"

int dma_map_setup_type_2x1_channel(enum dev_dma_map dev, enum dev_dma_map_type_2x1 map)
{
    unsigned char *base;
    unsigned int value;
    rt_base_t level;

    if (!(dev >= AES_W_DMA_MAP && dev <= SM3_DMA_MAP) || (unsigned)map > 1)
    {
        return 1;
    }
    level = rt_hw_interrupt_disable();
    base = (unsigned char *)LS_DMA_CHANNEL_MAP0;
    value = readl(base);
    value |= (0x3U << (dev * 2));
    value ^= (0x3U << (dev * 2));
    value |= ((unsigned int)map << (dev * 2));
    writel(value, base);
    rt_hw_interrupt_enable(level);
    return 0;
}

int dma_map_setup_type_4x2_channel(enum dev_dma_map dev, enum dev_dma_map_type_4x2 map)
{
    unsigned char *base;
    unsigned int offset_base;
    unsigned int value;
    rt_base_t level;

    if (!(dev >= UART_0_DMA_MAP && dev <= I2S_DMA_MAP) || (unsigned)map > 3)
    {
        return 1;
    }

    level = rt_hw_interrupt_disable();
    base = (dev >= UART_0_DMA_MAP && dev <= UART_7_DMA_MAP) ? (unsigned char *)LS_DMA_CHANNEL_MAP0
                                                            : (unsigned char *)LS_DMA_CHANNEL_MAP1;
    if (dev >= UART_0_DMA_MAP && dev <= UART_7_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP0;
        offset_base = dev * 2;
    }
    else
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        offset_base = 2 * (dev - UART_8_DMA_MAP);
    }

    value = readl(base);
    value |= (0x3U << offset_base);
    value ^= (0x3U << offset_base);
    value |= ((unsigned int)map << offset_base);
    writel(value, base);
    rt_hw_interrupt_enable(level);
    return 0;
}

int dma_map_setup_type_7x1_channel(enum dev_dma_map dev, enum dev_dma_map_type_7x1 map)
{
    unsigned char *base;
    unsigned int offset_base;
    unsigned int value;
    rt_base_t level;

    if (!(dev >= ADC_DMA_MAP && dev <= CAN_3_RX_DMA_MAP) || (unsigned)map > 7)
    {
        return 1;
    }

    level = rt_hw_interrupt_disable();
    if (dev >= ADC_DMA_MAP && dev <= CAN_2_RX_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        offset_base = 18 + (3 * (dev - ADC_DMA_MAP));
        value = readl(base);
        value |= (0x7U << offset_base);
        value ^= (0x7U << offset_base);
        value |= ((unsigned int)map << offset_base);
        writel(value, base);
    }
    else
    {
        // CAN_3_RX_DMA_MAP
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        value = readl(base);
        value |= (0x3U << 30);
        value ^= (0x3U << 30);
        value |= (((unsigned int)map & 0x3U) << 30);
        writel(value, base);

        base = (unsigned char *)LS_DMA_CHANNEL_MAP2;
        value = readl(base);
        value |= (0x1 << 0);
        value ^= (0x1 << 0);
        value |= (((map >> 2) & 0x1) << 0);
        writel(value, base);
    }
    rt_hw_interrupt_enable(level);
    return 0;
}

int dma_map_get_base(enum dev_dma_map dev)
{
    unsigned char *base;
    unsigned int value;
    unsigned int value_2;

    if (dev >= AES_W_DMA_MAP && dev <= SM3_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP0;
        value = readl(base);
        return ((value >> (dev * 2)) & 0x3);
    }
    if (dev >= UART_0_DMA_MAP && dev <= UART_7_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP0;
        value = readl(base);
        return ((value >> (dev * 2)) & 0x3) * 2;
    }
    if (dev >= UART_8_DMA_MAP && dev <= I2S_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        value = readl(base);
        return ((value >> ((dev - UART_8_DMA_MAP) * 2)) & 0x3) * 2;
    }
    if (dev >= ADC_DMA_MAP && dev <= CAN_2_RX_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        value = readl(base);
        return ((value >> (((dev - ADC_DMA_MAP) * 3) + 18)) & 0x7);
    }
    if (dev == CAN_3_RX_DMA_MAP)
    {
        base = (unsigned char *)LS_DMA_CHANNEL_MAP1;
        value = readl(base);
        value >>= 30;
        value &= 0x3;
        value_2 = readl(base + 0x4);
        value_2 &= 0x1;
        value |= (value_2 << 2);
        return value;
    }
    rt_kprintf("[%s]: [Error]: dev not exist %d\n", __func__, dev);
    return -1;
}
