/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: drv_framebuffer.c
 * Author: LoongsonLab
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 */

/*
 * Description:
 *   Register firmware-configured GOP scanout without calling EFI services.
 */

#include <rtdevice.h>
#include <rthw.h>
#include "board.h"
#include "display_test.h"

#ifdef BSP_USING_UEFI_FRAMEBUFFER
static struct rt_device lcd;
static struct rt_device_graphic_info graphics;

static rt_err_t framebuffer_control(rt_device_t device, int command, void *argument)
{
    RT_UNUSED(device);

    if (command == RTGRAPHIC_CTRL_GET_INFO && argument)
    {
        *(struct rt_device_graphic_info *)argument = graphics;
        return RT_EOK;
    }

    if (command == RTGRAPHIC_CTRL_RECT_UPDATE)
    {
        rt_hw_dmb();
        return RT_EOK;
    }

    /* PAN and VSYNC need the native display controller, not a false success. */
    return -RT_ENOSYS;
}

static const struct rt_device_ops framebuffer_ops = {
    .control = framebuffer_control,
};

static int framebuffer_init(void)
{
    const struct ls2k3000_framebuffer *fb = &ls2k3000_boot->framebuffer;

    if (ls2k3000_framebuffer_validate(fb) || fb->stride * 4 > 65535 ||
        fb->bytes > UINT32_MAX)
    {
        rt_kprintf("GOP: no supported linear RGB/BGR mode in firmware handoff\n");
        return -RT_ENOSYS;
    }

    graphics.pixel_format = fb->format ? RTGRAPHIC_PIXEL_FORMAT_ARGB888 :
                                        RTGRAPHIC_PIXEL_FORMAT_ABGR888;
    graphics.bits_per_pixel = 32;
    graphics.pitch = fb->stride * 4;
    graphics.width = fb->width;
    graphics.height = fb->height;
    graphics.framebuffer = (void *)LOONGARCH_PHYS_UNCACHED(fb->base);
    graphics.smem_len = fb->bytes;
    lcd.type = RT_Device_Class_Graphic;
    lcd.ops = &framebuffer_ops;
    return rt_device_register(&lcd, "lcd", RT_DEVICE_FLAG_RDWR);
}

INIT_DEVICE_EXPORT(framebuffer_init);

static long ls2k3000_colorbar(void)
{
    static const rt_uint32_t colors[] = {
        0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
        0x00ff00ff, 0x00ff0000, 0x000000ff, 0x00000000,
    };

    if (!graphics.framebuffer)
    {
        return -RT_ENOSYS;
    }

    for (unsigned int y = 0; y < graphics.height; ++y)
    {
        volatile rt_uint32_t *row = (void *)(graphics.framebuffer + y * graphics.pitch);

        for (unsigned int x = 0; x < graphics.width; ++x)
        {
            rt_uint32_t color = colors[(rt_uint64_t)x * 8 / graphics.width];

            if (graphics.pixel_format == RTGRAPHIC_PIXEL_FORMAT_ABGR888)
            {
                color = ((color & 0xff) << 16) | (color & 0xff00) | ((color >> 16) & 0xff);
            }

            row[x] = color;
        }
    }

    rt_hw_dmb();
    return RT_EOK;
}

MSH_CMD_EXPORT(ls2k3000_colorbar, Draw color bars on firmware GOP framebuffer);

long ls2k3000_display_test(void)
{
    const struct ls2k3000_framebuffer *fb = &ls2k3000_boot->framebuffer;
    struct ls2k3000_surface surface;
    int smart = 0;

    if (!graphics.framebuffer)
    {
        return -RT_ENOSYS;
    }

#ifdef RT_USING_SMART
    smart = 1;
#endif

    surface.pixels = (void *)graphics.framebuffer;
    surface.bytes = graphics.smem_len;
    surface.width = graphics.width;
    surface.height = graphics.height;
    surface.stride = fb->stride;
    surface.format = fb->format;

    if (ls2k3000_display_draw(&surface, RT_CPUS_NR, smart))
    {
        return -RT_EINVAL;
    }

    rt_hw_dmb();
    rt_kprintf("DISPLAY: startup screen %ux%u stride=%u format=%u\n",
               surface.width, surface.height, surface.stride, surface.format);
    return RT_EOK;
}

MSH_CMD_EXPORT(ls2k3000_display_test, Draw kernel startup screen and display checks);
#endif
