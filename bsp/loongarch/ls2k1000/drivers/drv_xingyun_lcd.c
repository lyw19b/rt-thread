/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    reuse validated Xingyun firmware scanout
 *
 * Description:
 *   Provide private drawing buffers while preserving U-Boot's DVO1 timing.
 *   Presentation copies to the reserved firmware framebuffer, so tearing is
 *   possible. No user pointer or clock/pinmux register is exposed or changed.
 */

#include <rtthread.h>
#ifdef BSP_USING_XINGYUN_LCD
#include <rthw.h>
#include <rtdevice.h>
#include <drivers/graphic.h>
#include "kernel_map.h"

#define XINGYUN_DC       0x80000000400c0000UL
#define XINGYUN_FB       0x0dc00000UL
#define XINGYUN_WIDTH    1024U
#define XINGYUN_HEIGHT   600U
#define XINGYUN_BYTES    (XINGYUN_WIDTH * XINGYUN_HEIGHT * 4U)

static struct rt_device lcd;
static struct rt_device_graphic_info info;
static struct rt_mutex lcd_lock;

static rt_uint32_t display_read(unsigned int offset)
{
    return HWREG32(XINGYUN_DC + offset);
}

static rt_err_t lcd_control(rt_device_t device, int command, void *argument)
{
    rt_err_t result = RT_EOK;

    RT_UNUSED(device);
    rt_mutex_take(&lcd_lock, RT_WAITING_FOREVER);

    switch (command)
    {
    case RTGRAPHIC_CTRL_GET_INFO:
        if (!argument)
        {
            result = -RT_EINVAL;
        }
        else
        {
            *(struct rt_device_graphic_info *)argument = info;
        }
        break;

    case RTGRAPHIC_CTRL_PAN_DISPLAY:
        if (argument != info.framebuffer &&
            argument != info.framebuffer + XINGYUN_BYTES)
        {
            result = -RT_EINVAL;
            break;
        }

        rt_memcpy((void *)LOONGARCH_PHYS_CACHED(XINGYUN_FB), argument, XINGYUN_BYTES);
        rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH,
                            (void *)LOONGARCH_PHYS_CACHED(XINGYUN_FB), XINGYUN_BYTES);
        rt_hw_dmb();
        break;

    default:
        result = -RT_ENOSYS;
        break;
    }

    rt_mutex_release(&lcd_lock);
    return result;
}

static const struct rt_device_ops lcd_ops =
{
    .control = lcd_control,
};

static int xingyun_lcd_init(void)
{
    rt_err_t result;

    /* DVO1 occupies the second register bank (+0x10) on LS2K1000LA. */
    if ((display_read(0x1250) & 0x107U) != 0x104U ||
        display_read(0x1270) != XINGYUN_FB ||
        display_read(0x1590) != XINGYUN_FB ||
        display_read(0x1290) != XINGYUN_WIDTH * 4 ||
        (display_read(0x1410) & 0xffffU) != XINGYUN_WIDTH ||
        (display_read(0x1490) & 0xffffU) != XINGYUN_HEIGHT)
    {
        rt_kprintf("Xingyun LCD: unsupported firmware mode; handoff refused\n");
        return -RT_EINVAL;
    }

    info.framebuffer = rt_malloc_align(XINGYUN_BYTES * 2, 64);

    if (!info.framebuffer)
    {
        return -RT_ENOMEM;
    }

    result = rt_mutex_init(&lcd_lock, "xingyun-lcd", RT_IPC_FLAG_PRIO);

    if (result != RT_EOK)
    {
        rt_free_align(info.framebuffer);
        return result;
    }

    rt_memset(info.framebuffer, 0, XINGYUN_BYTES * 2);
    info.pixel_format = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
    info.bits_per_pixel = 32;
    info.width = XINGYUN_WIDTH;
    info.height = XINGYUN_HEIGHT;
    info.pitch = XINGYUN_WIDTH * 4;
    info.smem_len = XINGYUN_BYTES * 2;
    lcd.type = RT_Device_Class_Graphic;
    lcd.ops = &lcd_ops;
    result = rt_device_register(&lcd, "lcd", RT_DEVICE_FLAG_RDWR);

    if (result != RT_EOK)
    {
        rt_mutex_detach(&lcd_lock);
        rt_free_align(info.framebuffer);
        return result;
    }

    rt_kprintf("Xingyun LCD: firmware DVO1 1024x600, scanout=0x%08x\n",
               (unsigned int)XINGYUN_FB);
    return RT_EOK;
}
INIT_DEVICE_EXPORT(xingyun_lcd_init);
#endif
