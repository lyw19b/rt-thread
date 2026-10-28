/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: ls2k3000_gop_copy_test.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Verify Explorer GOP pixel-pair writes and restore the tested rectangle.
 * Notes:
 *   Run novaui_stop first. This diagnostic briefly writes a 20x4 rectangle.
 *   Include only in the private acceptance image with the NovaUI service.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <drivers/graphic.h>
#include <stdint.h>
#include <stddef.h>

#if defined(BSP_USING_NOVAUI) && defined(BSP_NOVAUI_SINGLE_BUFFER)
extern int novaui_frame_copy_wide(void *destination, size_t bytes, uint32_t pitch,
                                  const uint32_t *source, uint32_t width, uint32_t height,
                                  int swap_red_blue);

static int ls2k3000_gop_copy_test(void)
{
    struct rt_device_graphic_info info;
    rt_device_t lcd = rt_device_find("lcd");
    uint32_t saved[80], source[69];
    unsigned mismatches = 0;
    unsigned restore_errors = 0;
    int result = 0;

    if (!lcd || rt_device_control(lcd, RTGRAPHIC_CTRL_GET_INFO, &info) ||
        !info.framebuffer || info.bits_per_pixel != 32 || info.width < 20 ||
        info.height < 4 || info.pitch < 80 || info.smem_len < (size_t)info.pitch * 4)
    {
        return -RT_EINVAL;
    }

    for (unsigned y = 0; y < 4; ++y)
    {
        volatile uint32_t *row = (void *)(info.framebuffer + y * info.pitch);

        for (unsigned x = 0; x < 20; ++x)
        {
            saved[y * 20 + x] = row[x];
        }
    }

    for (unsigned i = 0; i < 69; ++i)
    {
        source[i] = 0xff000000U | ((i * 0x123457U) & 0xffffffU);
    }

    for (unsigned offset = 1; offset <= 2; ++offset)
    {
        for (int swap = 0; swap < 2; ++swap)
        {
            result |= novaui_frame_copy_wide(info.framebuffer + offset * 4,
                                             info.smem_len - offset * 4, info.pitch,
                                             source + offset - 1, 17, 4, swap);
            rt_hw_dmb();

            for (unsigned y = 0; y < 4; ++y)
            {
                volatile uint32_t *row = (void *)(info.framebuffer + y * info.pitch);

                for (unsigned x = 0; x < 20; ++x)
                {
                    uint32_t expected = saved[y * 20 + x];

                    if (x >= offset && x < offset + 17)
                    {
                        expected = source[y * 17 + x - 1];

                        if (swap)
                        {
                            expected = (expected & 0xff00ff00U) |
                                       ((expected & 0xffU) << 16) |
                                       ((expected >> 16) & 0xffU);
                        }
                    }

                    mismatches += row[x] != expected;
                    row[x] = saved[y * 20 + x];
                }
            }

            rt_hw_dmb();

            for (unsigned y = 0; y < 4; ++y)
            {
                volatile uint32_t *row = (void *)(info.framebuffer + y * info.pitch);

                for (unsigned x = 0; x < 20; ++x)
                {
                    restore_errors += row[x] != saved[y * 20 + x];
                }
            }
        }
    }

    rt_kprintf("GOP WRITE64 %s checks=320 mismatches=%u result=%d restore_errors=%u\n",
               !result && !mismatches && !restore_errors ? "PASS" : "FAIL",
               mismatches, result, restore_errors);
    return result || mismatches || restore_errors ? -RT_ERROR : RT_EOK;
}

MSH_CMD_EXPORT(ls2k3000_gop_copy_test, Verify aligned GOP wide stores after novaui_stop);
#endif
