/*
 * Copyright (c) 2026, OpenLANPU Contributors
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    adapt ls2k300_mini_dp.dts LCD and touch
 * 2026-10-26     OpenLANPU Contributors    establish the complete display clock path
 *
 * Description:
 *   Configure the Hummingbird 1024x600 panel, GPIO84 backlight and I2C1 GT9xx.
 * Notes:
 *   The DTS supplies no touch reset GPIO. Preserve the panel's factory image
 *   and probe its two documented addresses without toggling an invented reset.
 */

#include <rtthread.h>
#include <rthw.h>
#include "ls2k.h"
#include "platform_interrupt.h"

#ifdef BSP_USING_HUMMINGBIRD
#include "hummingbird_panel.h"
#include <drivers/dev_pin.h>
/* The pinned graphics header contains no-op inlines with unused arguments. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <drivers/graphic.h>
#pragma GCC diagnostic pop
#include <ls2k0300_display.h>
#include <stdlib.h>

#define GPIO_BASE       0x16104000UL
#define GPIO_DIR        0x800
#define GPIO_OUT        0x900
#define GPIO_IN         0xa00
#define GPIO_IRQ_ENABLE 0xb00
#define GPIO_IRQ_POLARITY 0xc00
#define GPIO_IRQ_EDGE   0xd00
#define GPIO_IRQ_CLEAR  0xe00
#define GPIO_IRQ_STATUS 0xf00
#define GPIO_IRQ_BOTH   0xf80
#define TOUCH_PIN       82
#define TOUCH_IRQ       58
#define BACKLIGHT_PIN   84
#define PIXEL_PLL       0x16000410UL
#define PIXEL_SCALE     0x16000420UL
#define CLOCK_ENABLE    0x16000424UL
#define PIXEL_ENABLE    (1U << 6)
#define GMAC_NODE_CLOCK (1U << 8)
#define LCD_CONTROL     0x16091240UL
#define LCD_OUTPUT      (1U << 8)
#define CHIP_CONFIG3    0x1600010cUL
#define DC_COHERENT     ((1U << 12) | (1U << 4))
#define PIXEL_PARAMETERS HB_PANEL_PLL_PARAMETERS
#define CHIP_CONFIG4    0x16000110UL

static unsigned int touch_interrupts;

static void gpio_write(unsigned int offset, unsigned int pin, unsigned char value)
{
    HWREG8(LS2K_UNCACHED(GPIO_BASE + offset + pin)) = value;
    rt_hw_dmb();
}

static void touch_mask(int irq)
{
    RT_UNUSED(irq);
    gpio_write(GPIO_IRQ_ENABLE, TOUCH_PIN, 0);
}

static void touch_unmask(int irq)
{
    gpio_write(GPIO_IRQ_ENABLE, TOUCH_PIN, 1);
    rt_hw_interrupt_umask(irq);
}

static rt_bool_t touch_pending_ack(int irq)
{
    RT_UNUSED(irq);

    if (!(HWREG8(LS2K_UNCACHED(GPIO_BASE + GPIO_IRQ_STATUS + TOUCH_PIN)) & 1) ||
        !(HWREG8(LS2K_UNCACHED(GPIO_BASE + GPIO_IRQ_ENABLE + TOUCH_PIN)) & 1))
    {
        return RT_FALSE;
    }

    gpio_write(GPIO_IRQ_CLEAR, TOUCH_PIN, 1);
    touch_interrupts++;
    return RT_TRUE;
}

static const struct laxpu_touch_irq_ops touch_irq_ops = {
    .mask = touch_mask,
    .unmask = touch_unmask,
    .pending_ack = touch_pending_ack,
};

static int pixel_clock_prepare(void)
{
    rt_ubase_t reg = LS2K_UNCACHED(PIXEL_PLL);
    rt_uint32_t value = HWREG32(reg);
    rt_bool_t configured = (value & 0x7fffff00U) == PIXEL_PARAMETERS &&
                           (value & 0xb9U) == 0x89U;
    rt_base_t level;
    unsigned int wait;

    /* PIX PLL also supplies the GMAC backup clock. Retuning its VCO while
     * GMAC selects that source would change the network clock as well. */
    if (!configured && !(HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) & GMAC_NODE_CLOCK))
    {
        rt_kprintf("Hummingbird LCD: PIX PLL is in use by GMAC backup clock\n");
        return -RT_EBUSY;
    }

    /* Quiesce scanout even when the PLL already matches. Firmware may have
     * left output enabled; pinmux and timing must not change during scanout. */
    HWREG32(LS2K_UNCACHED(LCD_CONTROL)) &= ~LCD_OUTPUT;
    rt_hw_dmb();
    level = rt_hw_interrupt_disable();
    HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) &= ~PIXEL_ENABLE;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);

    /* Use the explicit board/panel working point. Stable wide color bars
     * do not establish fine-detail optical acceptance; see the profile. */
    if (configured)
    {
        goto enable_pixel_clock;
    }

    HWREG32(reg) = value & ~1U;
    rt_hw_dmb();
    HWREG32(reg) = (value & ~1U) | (1U << 4) | (1U << 3) | (1U << 5);
    rt_hw_dmb();
    /* Manual 3.3.1: install parameters with soft_set clear, power up,
     * then assert soft_set before waiting for the new PLL to lock. */
    HWREG32(reg) = PIXEL_PARAMETERS;
    rt_hw_dmb();
    HWREG32(reg) = PIXEL_PARAMETERS | (1U << 3);
    rt_hw_dmb();

    for (wait = 0; wait < 100; wait++)
    {
        if (HWREG32(reg) & (1U << 7))
        {
            HWREG32(reg) |= 1U;
            rt_hw_dmb();
            goto enable_pixel_clock;
        }

        rt_thread_mdelay(1);
    }

    rt_kprintf("Hummingbird LCD: pixel PLL lock timeout\n");
    return -RT_ETIMEOUT;

enable_pixel_clock:
    /* PLL lock is insufficient: the downstream divider and gate are separate
     * registers. Manual 3.4.29: mode 0, scale 7 is an undivided pixel clock.
     * Preserve every other peripheral's divider, gate and source selection. */
    level = rt_hw_interrupt_disable();
    HWREG32(LS2K_UNCACHED(PIXEL_SCALE)) =
        (HWREG32(LS2K_UNCACHED(PIXEL_SCALE)) & ~0xf0U) | 0x70U;
    HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) |= PIXEL_ENABLE;
    rt_hw_dmb();

    if ((HWREG32(reg) & 0x7fffffb9U) != (PIXEL_PARAMETERS | 0x89U) ||
        (HWREG32(LS2K_UNCACHED(PIXEL_SCALE)) & 0xf0U) != 0x70U ||
        !(HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) & PIXEL_ENABLE))
    {
        HWREG32(LS2K_UNCACHED(CLOCK_ENABLE)) &= ~PIXEL_ENABLE;
        rt_hw_dmb();
        rt_hw_interrupt_enable(level);
        rt_kprintf("Hummingbird LCD: clock configuration readback failed\n");
        return -RT_EIO;
    }

    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

static int panel_pad_prepare(void)
{
    rt_ubase_t reg = LS2K_UNCACHED(CHIP_CONFIG4);
    rt_base_t level = rt_hw_interrupt_disable();

    /* Do not inherit this electrical setting from a particular U-Boot.
     * Only DVO bits belong to us; retain UART/GMAC pads and bus ordering. */
    HWREG32(reg) = (HWREG32(reg) & ~HB_PANEL_PAD_MASK) | HB_PANEL_PAD_VALUE;
    rt_hw_dmb();
    rt_uint32_t value = HWREG32(reg);
    rt_hw_interrupt_enable(level);

    if ((value & HB_PANEL_PAD_MASK) != HB_PANEL_PAD_VALUE)
    {
        rt_kprintf("Hummingbird LCD: DVO pad readback failed\n");
        return -RT_EIO;
    }

    return RT_EOK;
}

int ls2k0300_hummingbird_panel_init(void)
{
    static const struct ls2k0300_lcd_timing timing = {
        .hfront = HB_PANEL_HFRONT,
        .hsync = HB_PANEL_HSYNC,
        .hback = HB_PANEL_HBACK,
        .vfront = HB_PANEL_VFRONT,
        .vsync = HB_PANEL_VSYNC,
        .vback = HB_PANEL_VBACK,
        .dvo_config = HB_PANEL_DVO_CONFIG,
    };
    rt_base_t level;
    int error = pixel_clock_prepare();

    if (error)
    {
        return error;
    }

    error = panel_pad_prepare();

    if (error)
    {
        return error;
    }

    rt_kprintf("Hummingbird LCD profile=%s optical=pending\n", HB_PANEL_PROFILE);

    level = rt_hw_interrupt_disable();
    /* DVO G0..27; I2C1 G50/51; GPIO82 INT and GPIO84 backlight. */
    HWREG32(LS2K_UNCACHED(0x16000490UL)) = 0xffffffffU;
    HWREG32(LS2K_UNCACHED(0x16000494UL)) |= 0x00ffffffU;
    HWREG32(LS2K_UNCACHED(0x1600049cUL)) |= 0xf0U;
    HWREG32(LS2K_UNCACHED(0x160004a4UL)) &= ~0x330U;

    /* Framebuffers are cached RAM. The LA264 cache helpers only issue dbar;
     * route this display master's reads through CCIO instead of bypassing
     * dirty CPU cache lines. Manual 3.4.4: bits 4 and 12 belong only to DC. */
    HWREG32(LS2K_UNCACHED(CHIP_CONFIG3)) |= DC_COHERENT;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);

    error = ls2k0300_lcd_register_timing((void *)LS2K_UNCACHED(0x16090000UL), 51, &timing);

    if (error)
    {
        rt_kprintf("Hummingbird LCD: registration failed (%d)\n", error);
        return error;
    }

    gpio_write(GPIO_OUT, BACKLIGHT_PIN, 1);
    gpio_write(GPIO_DIR, BACKLIGHT_PIN, 0);
    touch_mask(TOUCH_IRQ);
    gpio_write(GPIO_DIR, TOUCH_PIN, 1);
    gpio_write(GPIO_IRQ_BOTH, TOUCH_PIN, 0);
    gpio_write(GPIO_IRQ_POLARITY, TOUCH_PIN, 0);
    gpio_write(GPIO_IRQ_EDGE, TOUCH_PIN, 1);
    gpio_write(GPIO_IRQ_CLEAR, TOUCH_PIN, 1);
    liointc_set_irq_mode(TOUCH_IRQ, PIN_IRQ_MODE_HIGH_LEVEL);

    error = ls2k0300_i2c_register((void *)LS2K_UNCACHED(0x16109000UL), 200000000, "i2c1");

    if (!error)
    {
        error = laxpu_gt9147_register("i2c1", 0x5d, TOUCH_IRQ, &touch_irq_ops);

        if (error)
        {
            error = laxpu_gt9147_register("i2c1", 0x14, TOUCH_IRQ, &touch_irq_ops);
        }
    }

    if (error)
    {
        touch_mask(TOUCH_IRQ);
        rt_kprintf("Hummingbird touch: I2C1 GT9xx probe failed (%d)\n", error);
    }

    return error;
}

static long hummingbird_panel_status(void)
{
    rt_kprintf("Hummingbird LCD cfg=%08x dvo=%08x irq=%08x pll=%08x\n",
               HWREG32(LS2K_UNCACHED(0x16091240UL)),
               HWREG32(LS2K_UNCACHED(0x160913c0UL)),
               HWREG32(LS2K_UNCACHED(0x16091570UL)),
               HWREG32(LS2K_UNCACHED(PIXEL_PLL)));
    rt_kprintf("Hummingbird touch GPIO82=%u latch=%u interrupts=%u backlight=%u\n",
               HWREG8(LS2K_UNCACHED(GPIO_BASE + GPIO_IN + TOUCH_PIN)),
               HWREG8(LS2K_UNCACHED(GPIO_BASE + GPIO_IRQ_STATUS + TOUCH_PIN)),
               touch_interrupts,
               HWREG8(LS2K_UNCACHED(GPIO_BASE + GPIO_OUT + BACKLIGHT_PIN)));

    rt_kprintf("Hummingbird LCD fb0=%08x fb1=%08x scan=%08x tick=%u coher=%08x\n",
               HWREG32(LS2K_UNCACHED(0x16091260UL)),
               HWREG32(LS2K_UNCACHED(0x16091580UL)),
               HWREG32(LS2K_UNCACHED(0x160914c0UL)), (unsigned int)rt_tick_get(),
               HWREG32(LS2K_UNCACHED(CHIP_CONFIG3)));

    rt_kprintf("Hummingbird LCD mux=%08x/%08x scale=%08x gate=%08x pad=%08x\n",
               HWREG32(LS2K_UNCACHED(0x16000490UL)),
               HWREG32(LS2K_UNCACHED(0x16000494UL)),
               HWREG32(LS2K_UNCACHED(0x16000420UL)),
               HWREG32(LS2K_UNCACHED(0x16000424UL)),
               HWREG32(LS2K_UNCACHED(0x16000110UL)));
    rt_kprintf("Hummingbird LCD origin=%08x high=%08x/%08x dither=%08x timing=%08x\n",
               HWREG32(LS2K_UNCACHED(0x16091300UL)),
               HWREG32(LS2K_UNCACHED(0x160915a0UL)),
               HWREG32(LS2K_UNCACHED(0x160915c0UL)),
               HWREG32(LS2K_UNCACHED(0x16091360UL)),
               HWREG32(LS2K_UNCACHED(0x160913e0UL)));

    return 0;
}

MSH_CMD_EXPORT(hummingbird_panel_status, Show physical LCD and I2C1 touch state);

#ifdef BSP_USING_SELFTEST
/* Read both raw scan-counter fields without assuming their axis or units.
 * Yield between batches so the watchdog and other diagnostic tasks can run. */
static long hummingbird_panel_scan(void)
{
    rt_uint32_t minimum[2] = {0xffffU, 0xffffU};
    rt_uint32_t maximum[2] = {0, 0};
    rt_uint32_t samples = 0;
    rt_tick_t start = rt_tick_get();

    while (rt_tick_get() - start < RT_TICK_PER_SECOND)
    {
        for (unsigned i = 0; i < 4096; i++)
        {
            rt_uint32_t position = HWREG32(LS2K_UNCACHED(0x160914c0UL));
            rt_uint32_t field[2] = {position & 0xffffU, position >> 16};

            for (unsigned axis = 0; axis < 2; axis++)
            {
                if (field[axis] < minimum[axis])
                {
                    minimum[axis] = field[axis];
                }

                if (field[axis] > maximum[axis])
                {
                    maximum[axis] = field[axis];
                }
            }

            samples++;
        }

        rt_thread_delay(1);
    }

    rt_kprintf("LCD SCAN samples=%u low=%u..%u high=%u..%u ticks=%u\n",
               samples, minimum[0], maximum[0], minimum[1], maximum[1],
               (unsigned)(rt_tick_get() - start));
    rt_kprintf("LCD MODE hdisplay=%08x hsync=%08x vdisplay=%08x vsync=%08x stride=%u\n",
               HWREG32(LS2K_UNCACHED(0x16091400UL)),
               HWREG32(LS2K_UNCACHED(0x16091420UL)),
               HWREG32(LS2K_UNCACHED(0x16091480UL)),
               HWREG32(LS2K_UNCACHED(0x160914a0UL)),
               HWREG32(LS2K_UNCACHED(0x16091280UL)));

    return 0;
}

MSH_CMD_EXPORT(hummingbird_panel_scan, Sample raw scan counters without changing mode);

/* Compare the visible active window while preserving the line period, sync,
 * stride and framebuffer bytes. Run only in the standalone diagnostic image. */
static long hummingbird_panel_window(void)
{
    rt_ubase_t reg = LS2K_UNCACHED(0x16091400UL);
    rt_uint32_t saved = HWREG32(reg);
    rt_uint32_t trial = (saved & 0xffff0000U) | 512U;
    rt_uint32_t observed;
    rt_uint32_t restored;
    rt_err_t result = RT_EOK;

    if ((saved & 0xffffU) != 1024U)
    {
        rt_kprintf("LCD WINDOW requires original active width 1024\n");
        return -RT_EINVAL;
    }

    HWREG32(reg) = trial;
    rt_hw_dmb();
    rt_thread_mdelay(100);
    observed = HWREG32(reg);
    rt_kprintf("LCD WINDOW saved=%08x trial=%08x observed=%08x\n",
               saved, trial, observed);

    if (observed == trial)
    {
        rt_kprintf("LCD WINDOW active=512 for 30 seconds; total and stride unchanged\n");
        hummingbird_panel_scan();
        rt_thread_mdelay(29000);
    }
    else
    {
        result = -RT_ERROR;
    }

    HWREG32(reg) = saved;
    rt_hw_dmb();
    rt_thread_mdelay(100);
    restored = HWREG32(reg);

    if (restored != saved)
    {
        result = -RT_ERROR;
    }

    rt_kprintf("LCD WINDOW restored=%08x status=%d; visual confirmation required\n",
               restored, result);
    return result;
}

MSH_CMD_EXPORT(hummingbird_panel_window, Test 512 active pixels for 30 seconds then restore);

/* GPIO0/1/2/3 share PCLK/VSYNC/HSYNC/DE. Read the packed input byte only;
 * never change mux or direction. Availability in alternate mode is unproven.
 * Counts are asynchronous samples, not connector waveform measurements. */
static long hummingbird_panel_pins(void)
{
    static const char *names[] = {"pclk", "vsync", "hsync", "de"};
    rt_uint32_t high[4] = {0};
    rt_uint32_t changes[4] = {0};
    rt_uint32_t samples = 0;
    rt_uint8_t previous = HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x20));
    rt_tick_t start = rt_tick_get();

    while (rt_tick_get() - start < RT_TICK_PER_SECOND)
    {
        for (unsigned i = 0; i < 4096; i++)
        {
            rt_uint8_t value = HWREG8(LS2K_UNCACHED(GPIO_BASE + 0x20));

            for (unsigned pin = 0; pin < 4; pin++)
            {
                high[pin] += (value >> pin) & 1U;
                changes[pin] += ((value ^ previous) >> pin) & 1U;
            }

            previous = value;
            samples++;
        }

        rt_thread_delay(1);
    }

    rt_kprintf("LCD PINS samples=%u ticks=%u mux=%08x\n", samples,
               (unsigned)(rt_tick_get() - start),
               HWREG32(LS2K_UNCACHED(0x16000490UL)));

    for (unsigned pin = 0; pin < 4; pin++)
    {
        rt_kprintf("LCD PIN %s high=%u changes=%u\n", names[pin], high[pin], changes[pin]);
    }

    rt_kprintf("LCD PINS asynchronous GPIO readback only; not a measured pixel clock\n");
    return 0;
}

MSH_CMD_EXPORT(hummingbird_panel_pins, Read GPIO input samples while preserving LCD mux);

/* Initialization requests zero origin, yet the physical board reads 0x40004.
 * Retry once after scanout is running, then restore even if readback fails. */
static long hummingbird_panel_origin(void)
{
    rt_ubase_t reg = LS2K_UNCACHED(0x16091300UL);
    rt_uint32_t saved = HWREG32(reg);
    int error = 0;

    HWREG32(reg) = 0;
    rt_hw_dmb();
    rt_thread_mdelay(100);
    rt_kprintf("LCD ORIGIN saved=%08x requested=00000000 readback=%08x\n",
               saved, HWREG32(reg));

    if (HWREG32(reg))
    {
        error = -RT_ERROR;
    }
    else
    {
        rt_kprintf("LCD ORIGIN observing seconds=30\n");
        rt_thread_mdelay(30000);
    }

    HWREG32(reg) = saved;
    rt_hw_dmb();
    rt_thread_mdelay(100);

    if (HWREG32(reg) != saved)
    {
        error = -RT_ERROR;
    }

    rt_kprintf("LCD ORIGIN restored=%08x status=%d\n", HWREG32(reg), error);
    return error;
}

MSH_CMD_EXPORT(hummingbird_panel_origin, Retry zero origin briefly then restore);

/* Manual 3.4.29: pix_freqscale=9 selects integer divide-by-two.
 * Preserve all other clocks and restore after a bounded, diagnostic-only hold. */
static long hummingbird_panel_halfclock(void)
{
    rt_device_t device = rt_device_find("lcd");
    rt_ubase_t reg = LS2K_UNCACHED(0x16000420UL);
    rt_uint32_t saved = HWREG32(reg) & 0xf0U;
    unsigned frames = 0;
    unsigned timeouts = 0;
    rt_tick_t start;
    int error = 0;

    if (!device || saved != 0x70U)
    {
        return -RT_EINVAL;
    }

    HWREG32(reg) = (HWREG32(reg) & ~0xf0U) | 0x90U;
    rt_hw_dmb();
    rt_thread_mdelay(100);
    rt_kprintf("LCD HALFCLOCK begin seconds=30 scale=%08x\n", HWREG32(reg));
    start = rt_tick_get();

    if ((HWREG32(reg) & 0xf0U) != 0x90U)
    {
        error = -RT_ERROR;
    }
    else
    {
        while (rt_tick_get() - start < 30 * RT_TICK_PER_SECOND)
        {
            if (rt_device_control(device, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL))
            {
                timeouts++;
            }
            else
            {
                frames++;
            }
        }
    }

    rt_kprintf("LCD HALFCLOCK frames=%u timeouts=%u ticks=%u tick_hz=%u\n",
               frames, timeouts, (unsigned)(rt_tick_get() - start), RT_TICK_PER_SECOND);
    HWREG32(reg) = (HWREG32(reg) & ~0xf0U) | saved;
    rt_hw_dmb();
    rt_thread_mdelay(100);

    if ((HWREG32(reg) & 0xf0U) != saved || timeouts)
    {
        error = -RT_ERROR;
    }

    rt_kprintf("LCD HALFCLOCK restored scale=%08x status=%d\n", HWREG32(reg), error);
    return error;
}

MSH_CMD_EXPORT(hummingbird_panel_halfclock, Halve display clock for 30 seconds then restore);

/* The supplied ATK STM32F429 example uses 45 MHz. Test both clock phases
 * with unchanged geometry, then restore the normal board configuration. */
static long hummingbird_panel_vendor(void)
{
    rt_device_t device = rt_device_find("lcd");
    rt_ubase_t pll = LS2K_UNCACHED(PIXEL_PLL);
    rt_ubase_t config = LS2K_UNCACHED(0x16091240UL);
    rt_ubase_t dvo = LS2K_UNCACHED(0x160913c0UL);
    rt_uint32_t saved_config = HWREG32(config) & ~(1U << 7);
    rt_uint32_t saved_dvo = HWREG32(dvo);
    rt_uint32_t saved_pll = HWREG32(pll);
    rt_uint32_t parameters = (32U << 24) | (72U << 15) | (6U << 8);
    int result = -RT_ETIMEOUT;
    int restored;

    if (!device || (saved_pll & 0x7fffff00U) != PIXEL_PARAMETERS ||
        (saved_pll & 0xb1U) != 0x81U || !(saved_config & (1U << 8)) ||
        (HWREG32(LS2K_UNCACHED(0x16000420UL)) & 0xf0U) != 0x70U)
    {
        return -RT_EINVAL;
    }

    HWREG32(config) = saved_config & ~(1U << 8);
    rt_hw_dmb();
    rt_thread_mdelay(20);
    HWREG32(pll) = saved_pll & ~1U;
    rt_hw_dmb();
    HWREG32(pll) = (saved_pll & ~1U) | (1U << 4) | (1U << 3) | (1U << 5);
    rt_hw_dmb();
    /* SYSCLK / refdiv * loop / outdiv = 120 / 6 * 72 / 32 MHz. */
    HWREG32(pll) = parameters;
    rt_hw_dmb();
    HWREG32(pll) = parameters | (1U << 3);
    rt_hw_dmb();

    for (unsigned wait = 0; wait < 100; wait++)
    {
        if (HWREG32(pll) & (1U << 7))
        {
            HWREG32(pll) |= 1U;
            rt_hw_dmb();
            result = RT_EOK;
            break;
        }

        rt_thread_mdelay(1);
    }

    if (result == RT_EOK && (HWREG32(pll) & 0x7fffffb1U) != (parameters | 0x81U))
    {
        result = -RT_ERROR;
    }

    for (unsigned phase = 0; phase < 2 && result == RT_EOK; phase++)
    {
        unsigned frames = 0;
        unsigned timeouts = 0;
        rt_tick_t start;

        HWREG32(dvo) = (saved_dvo & ~(1U << 9)) | (phase << 9);
        HWREG32(config) = saved_config;
        rt_hw_dmb();
        rt_thread_mdelay(100);

        if (HWREG32(dvo) != ((saved_dvo & ~(1U << 9)) | (phase << 9)))
        {
            result = -RT_ERROR;
            break;
        }

        rt_kprintf("LCD VENDOR begin phase=%u seconds=30 pll=%08x dvo=%08x\n",
                   phase, HWREG32(pll), HWREG32(dvo));
        start = rt_tick_get();

        while (rt_tick_get() - start < 30 * RT_TICK_PER_SECOND)
        {
            if (rt_device_control(device, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL))
            {
                timeouts++;
            }
            else
            {
                frames++;
            }
        }

        rt_kprintf("LCD VENDOR phase=%u frames=%u timeouts=%u ticks=%u tick_hz=%u\n",
                   phase, frames, timeouts, (unsigned)(rt_tick_get() - start),
                   RT_TICK_PER_SECOND);

        if (timeouts || !frames)
        {
            result = -RT_ERROR;
        }
    }

    restored = pixel_clock_prepare();
    HWREG32(dvo) = saved_dvo;
    HWREG32(config) = restored == RT_EOK ? saved_config : saved_config & ~(1U << 8);
    rt_hw_dmb();
    rt_thread_mdelay(100);

    if (restored || HWREG32(dvo) != saved_dvo ||
        (HWREG32(config) & 0x107U) != (saved_config & 0x107U))
    {
        result = -RT_ERROR;
    }

    rt_kprintf("LCD VENDOR restored pll=%08x dvo=%08x status=%d\n",
               HWREG32(pll), HWREG32(dvo), result);
    return result;
}

MSH_CMD_EXPORT(hummingbird_panel_vendor, Test vendor 45 MHz at two phases then restore);

/* Format/stride readback can lag a write on the physical controller.
 * Bound the wait and verify output enable too, instead of trusting dbar alone. */
static int panel_wait_format(rt_uint32_t config, rt_uint32_t stride)
{
    for (unsigned elapsed = 0; elapsed < 100; elapsed++)
    {
        if ((HWREG32(LS2K_UNCACHED(0x16091240UL)) & 0x107U) == (config & 0x107U) &&
            HWREG32(LS2K_UNCACHED(0x16091280UL)) == stride)
        {
            return 0;
        }

        rt_thread_mdelay(1);
    }

    return -RT_ETIMEOUT;
}

/* Exclusive diagnostic: preserve both buffers, display RGB565 for 30 seconds,
 * then restore the exact previous bytes, stride and framebuffer configuration.
 * This does not change the registered graphics device's public pixel format. */
static long hummingbird_panel_rgb565(void)
{
    struct rt_device_graphic_info info;
    rt_device_t device = rt_device_find("lcd");
    rt_ubase_t config_reg = LS2K_UNCACHED(0x16091240UL);
    rt_ubase_t stride_reg = LS2K_UNCACHED(0x16091280UL);
    rt_uint32_t config;
    rt_uint32_t stride;
    rt_uint32_t physical;
    rt_size_t frame_bytes;
    rt_uint8_t *backup;
    volatile rt_uint32_t *uncached;
    int error = 0;

    if (!device || rt_device_control(device, RTGRAPHIC_CTRL_GET_INFO, &info))
    {
        return -RT_ENOSYS;
    }

    frame_bytes = info.pitch * info.height;
    physical = HWREG32(LS2K_UNCACHED(0x16091260UL));

    if (info.width != 1024 || info.height != 600 || info.pitch != 4096 ||
        info.smem_len != frame_bytes * 2 || physical >= 0x10000000U ||
        info.smem_len > 0x10000000U - physical ||
        HWREG32(LS2K_UNCACHED(0x16091580UL)) != physical + frame_bytes ||
        HWREG32(LS2K_UNCACHED(0x160915a0UL)) ||
        HWREG32(LS2K_UNCACHED(0x160915c0UL)))
    {
        return -RT_EINVAL;
    }

    backup = rt_malloc(info.smem_len);

    if (!backup)
    {
        return -RT_ENOMEM;
    }

    rt_memcpy(backup, info.framebuffer, info.smem_len);
    config = HWREG32(config_reg);
    stride = HWREG32(stride_reg);
    uncached = (volatile rt_uint32_t *)LS2K_UNCACHED(physical);
    /* Output enable is bit 8; reset_n is bit 20. Preserve the active buffer. */
    HWREG32(config_reg) = config & ~(1U << 8);
    rt_hw_dmb();
    rt_thread_mdelay(20);

    for (unsigned buffer = 0; buffer < 2; buffer++)
    {
        rt_uint16_t *pixels = (rt_uint16_t *)(info.framebuffer + buffer * frame_bytes);

        for (unsigned y = 0; y < info.height; y++)
        {
            for (unsigned x = 0; x < info.width; x++)
            {
                pixels[y * info.width + x] =
                    x >= info.width / 3 && x < info.width * 2 / 3 ? 0xf800U : 0xffffU;
            }
        }
    }

    for (unsigned i = 0; i < info.smem_len / sizeof(*uncached); i++)
    {
        uncached[i] = ((rt_uint32_t *)info.framebuffer)[i];
    }

    rt_hw_dmb();
    HWREG32(stride_reg) = info.width * 2;
    HWREG32(config_reg) = (config & ~7U) | 3U;
    rt_hw_dmb();
    error = panel_wait_format((config & ~7U) | 3U, info.width * 2);
    rt_kprintf("LCD RGB565 begin seconds=30 cfg=%08x stride=%u\n",
               HWREG32(config_reg), HWREG32(stride_reg));

    if (!error)
    {
        rt_thread_mdelay(30000);
    }

    HWREG32(config_reg) = config & ~(1U << 8);
    rt_hw_dmb();
    rt_thread_mdelay(20);
    rt_memcpy(info.framebuffer, backup, info.smem_len);

    for (unsigned i = 0; i < info.smem_len / sizeof(*uncached); i++)
    {
        uncached[i] = ((rt_uint32_t *)backup)[i];
    }

    rt_hw_dmb();
    HWREG32(stride_reg) = stride;
    HWREG32(config_reg) = config;
    rt_hw_dmb();

    if (panel_wait_format(config, stride) ||
        rt_memcmp(info.framebuffer, backup, info.smem_len))
    {
        error = -RT_ERROR;
    }

    rt_free(backup);
    rt_kprintf("LCD RGB565 restored cfg=%08x stride=%u status=%d\n",
               HWREG32(config_reg), HWREG32(stride_reg), error);
    return error;
}

MSH_CMD_EXPORT(hummingbird_panel_rgb565, Test RGB565 for 30 seconds then restore);

/* Manual 3.4: CHIP_CONFIG4 bits 11:10 select DVO pad current only.
 * Test legal strengths with the same mode, then restore the original bits. */
static long hummingbird_panel_drive(int argc, char **argv)
{
    static const unsigned milliamps[] = {2, 4, 8, 12};
    rt_ubase_t reg = LS2K_UNCACHED(0x16000110UL);
    unsigned long strength;
    unsigned long seconds;
    rt_uint32_t saved;
    rt_uint32_t value;
    char *end;
    int error = 0;

    if (argc != 3)
    {
        rt_kprintf("Usage: hummingbird_panel_drive strength(0..3) seconds(1..30)\n");
        return -RT_EINVAL;
    }

    strength = strtoul(argv[1], &end, 10);

    if (!argv[1][0] || *end || strength > 3)
    {
        return -RT_EINVAL;
    }

    seconds = strtoul(argv[2], &end, 10);

    if (!argv[2][0] || *end || seconds < 1 || seconds > 30)
    {
        return -RT_EINVAL;
    }

    saved = HWREG32(reg) & (3U << 10);
    value = (HWREG32(reg) & ~(3U << 10)) | ((rt_uint32_t)strength << 10);
    HWREG32(reg) = value;
    rt_hw_dmb();
    rt_kprintf("LCD DRIVE begin ma=%u seconds=%u pad=%08x\n",
               milliamps[strength], (unsigned)seconds, HWREG32(reg));

    if (HWREG32(reg) != value)
    {
        error = -RT_ERROR;
    }
    else
    {
        rt_thread_mdelay(seconds * 1000);
    }

    HWREG32(reg) = (HWREG32(reg) & ~(3U << 10)) | saved;
    rt_hw_dmb();
    rt_kprintf("LCD DRIVE restored pad=%08x\n", HWREG32(reg));

    if ((HWREG32(reg) & (3U << 10)) != saved)
    {
        error = -RT_ERROR;
    }

    return error;
}

/* Diagnostic only: exercise the physical backlight, then compare the two
 * pixel-clock phases with an opaque pattern in BOTH scanout buffers. Never
 * run while NovaUIOS owns the LCD. Bit 9 is the DVO clock polarity control;
 * do not confuse it with the upper bit in firmware's 0x80001111 value. */
static long hummingbird_panel_probe(int argc, char **argv)
{
    struct rt_device_graphic_info info;
    rt_device_t device = rt_device_find("lcd");
    rt_uint32_t *pixels;
    rt_uint32_t dvo;
    rt_uint32_t coherent;
    rt_bool_t direct_ddr;
    rt_bool_t solid;
    unsigned long color = 0;
    char *end;
    unsigned phase;
    int error;

    if ((argc < 2 || argc > 4) ||
        (rt_strcmp(argv[1], "0") && rt_strcmp(argv[1], "1")) ||
        (argc >= 3 && rt_strcmp(argv[2], "ddr") && rt_strcmp(argv[2], "ccio")))
    {
        rt_kprintf("Usage: hummingbird_panel_probe 0|1 [ddr|ccio [RRGGBB]]\n");
        return -RT_EINVAL;
    }

    solid = argc == 4;

    if (solid)
    {
        color = strtoul(argv[3], &end, 16);

        if (!argv[3][0] || *end || color > 0xffffffUL)
        {
            return -RT_EINVAL;
        }
    }

    if (!device)
    {
        return -RT_ENOSYS;
    }

    phase = argv[1][0] - '0';
    direct_ddr = argc >= 3 && !rt_strcmp(argv[2], "ddr");
    error = rt_device_control(device, RTGRAPHIC_CTRL_GET_INFO, &info);

    if (error)
    {
        return error;
    }

    for (unsigned blink = 0; !solid && blink < 2; blink++)
    {
        gpio_write(GPIO_OUT, BACKLIGHT_PIN, 0);
        rt_thread_mdelay(400);
        gpio_write(GPIO_OUT, BACKLIGHT_PIN, 1);
        rt_thread_mdelay(400);
    }

    pixels = (rt_uint32_t *)info.framebuffer;

    for (unsigned i = 0; i < info.smem_len / sizeof(*pixels); i++)
    {
        /* White field with a central red stripe, alpha set explicitly. */
        unsigned x = i % (info.pitch / sizeof(*pixels));

        if (solid)
        {
            pixels[i] = 0xff000000U | (rt_uint32_t)color;
        }
        else
        {
            pixels[i] = x >= info.width / 3 && x < info.width * 2 / 3 ?
                        0xffff0000U : 0xffffffffU;
        }
    }

    error = rt_device_control(device, RTGRAPHIC_CTRL_RECT_UPDATE, RT_NULL);

    if (error)
    {
        return error;
    }

    if (direct_ddr)
    {
        volatile rt_uint32_t *uncached;
        rt_ubase_t physical = HWREG32(LS2K_UNCACHED(0x16091260UL));

        /* The LCD driver owns contiguous low RAM. Write identical values to
         * its uncached alias before bypassing CCIO; no UI may run here. */
        if (physical >= 0x10000000UL ||
            info.smem_len > 0x10000000UL - physical ||
            HWREG32(LS2K_UNCACHED(0x160915a0UL)))
        {
            return -RT_EINVAL;
        }

        uncached = (volatile rt_uint32_t *)LS2K_UNCACHED(physical);

        for (unsigned i = 0; i < info.smem_len / sizeof(*pixels); i++)
        {
            uncached[i] = pixels[i];
        }

        rt_hw_dmb();
    }

    coherent = HWREG32(LS2K_UNCACHED(CHIP_CONFIG3));
    coherent |= DC_COHERENT;

    if (direct_ddr)
    {
        coherent &= ~(1U << 12);
    }

    HWREG32(LS2K_UNCACHED(CHIP_CONFIG3)) = coherent;
    rt_hw_dmb();
    dvo = HWREG32(LS2K_UNCACHED(0x160913c0UL));
    HWREG32(LS2K_UNCACHED(0x160913c0UL)) = (dvo & ~(1U << 9)) | (phase << 9);
    rt_hw_dmb();
    rt_kprintf("LCD PROBE phase=%u route=%s ", phase, direct_ddr ? "DDR" : "CCIO");

    if (solid)
    {
        rt_kprintf("solid=%06x", (unsigned)color);
    }
    else
    {
        rt_kprintf("white/red/white");
    }

    rt_kprintf("; visual confirmation required\n");
    return hummingbird_panel_status();
}

/* Keep the mode, PLL and framebuffer fixed. Only test documented output
 * polarity bits; restore them after every bounded observation window.
 * Mask bits: 0 pixel clock, 1 DE, 2 HSYNC, 3 VSYNC. This is a diagnostic
 * override, not a replacement for the panel's specified bus flags. */
static long hummingbird_panel_signal(int argc, char **argv)
{
    rt_device_t device = rt_device_find("lcd");
    rt_ubase_t dvo_reg = LS2K_UNCACHED(0x160913c0UL);
    rt_ubase_t hsync_reg = LS2K_UNCACHED(0x16091420UL);
    rt_ubase_t vsync_reg = LS2K_UNCACHED(0x160914a0UL);
    rt_uint32_t saved_dvo;
    rt_uint32_t saved_hsync;
    rt_uint32_t saved_vsync;
    rt_uint32_t dvo;
    rt_uint32_t hsync;
    rt_uint32_t vsync;
    rt_tick_t start;
    rt_tick_t elapsed;
    rt_tick_t duration;
    unsigned long mask;
    unsigned long seconds;
    unsigned frames = 0;
    unsigned timeouts = 0;
    char *end;
    int result = RT_EOK;

    if (argc != 3 || !device)
    {
        rt_kprintf("Usage: hummingbird_panel_signal mask(0..15) seconds(1..30)\n");
        return -RT_EINVAL;
    }

    mask = strtoul(argv[1], &end, 10);

    if (!argv[1][0] || *end || mask > 15)
    {
        return -RT_EINVAL;
    }

    seconds = strtoul(argv[2], &end, 10);

    if (!argv[2][0] || *end || !seconds || seconds > 30)
    {
        return -RT_EINVAL;
    }

    saved_dvo = HWREG32(dvo_reg);
    saved_hsync = HWREG32(hsync_reg);
    saved_vsync = HWREG32(vsync_reg);
    dvo = (saved_dvo & ~((1U << 9) | (1U << 1))) |
          ((mask & 1U) << 9) | (mask & 2U);
    hsync = (saved_hsync & ~(1U << 31)) | ((rt_uint32_t)(mask & 4U) << 29);
    vsync = (saved_vsync & ~(1U << 31)) | ((rt_uint32_t)(mask & 8U) << 28);

    HWREG32(dvo_reg) = dvo;
    HWREG32(hsync_reg) = hsync;
    HWREG32(vsync_reg) = vsync;
    rt_hw_dmb();
    rt_kprintf("LCD SIGNAL begin mask=%u seconds=%u dvo=%08x hsync=%08x vsync=%08x\n",
               (unsigned)mask, (unsigned)seconds, HWREG32(dvo_reg),
               HWREG32(hsync_reg), HWREG32(vsync_reg));

    if (HWREG32(dvo_reg) != dvo || HWREG32(hsync_reg) != hsync ||
        HWREG32(vsync_reg) != vsync)
    {
        rt_kprintf("LCD SIGNAL polarity readback mismatch\n");
        result = -RT_ERROR;
    }
    else
    {
        start = rt_tick_get();
        duration = rt_tick_from_millisecond(seconds * 1000);

        do
        {
            if (rt_device_control(device, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL))
            {
                timeouts++;
            }
            else
            {
                frames++;
            }

            elapsed = rt_tick_get() - start;
        } while (elapsed < duration);

        rt_kprintf("LCD SIGNAL frames=%u timeouts=%u ticks=%u tick_hz=%u cfg=%08x\n",
                   frames, timeouts, (unsigned)elapsed, RT_TICK_PER_SECOND,
                   HWREG32(LS2K_UNCACHED(0x16091240UL)));

        if (timeouts || !frames)
        {
            result = -RT_ERROR;
        }
    }

    HWREG32(dvo_reg) = saved_dvo;
    HWREG32(hsync_reg) = saved_hsync;
    HWREG32(vsync_reg) = saved_vsync;
    rt_hw_dmb();
    rt_kprintf("LCD SIGNAL restored mask=%u dvo=%08x hsync=%08x vsync=%08x\n",
               (unsigned)mask, HWREG32(dvo_reg), HWREG32(hsync_reg), HWREG32(vsync_reg));
    return result;
}

/* Run with no renderer. The default also checks panel_probe's known bars;
 * "status" leaves arbitrary static images intact and samples only DC status.
 * Peripheral IRQ masking does not stop the OS timer. */
static long hummingbird_panel_dma(int argc, char **argv)
{
    struct rt_device_graphic_info info;
    rt_device_t device = rt_device_find("lcd");
    rt_ubase_t status_reg = LS2K_UNCACHED(0x16091570UL);
    rt_ubase_t physical = HWREG32(LS2K_UNCACHED(0x16091260UL));
    volatile rt_uint32_t *uncached;
    rt_uint32_t *cached;
    rt_uint32_t saved_enable;
    rt_uint32_t test_enable;
    rt_uint32_t observed = 0;
    rt_tick_t start;
    unsigned memory_errors = 0;
    unsigned frames = 0;
    unsigned completed = 0;
    unsigned underflow = 0;
    unsigned fatal = 0;
    unsigned anchored = 0;
    rt_bool_t status_only = argc == 2 && !rt_strcmp(argv[1], "status");

    if (argc != 1 && !status_only)
    {
        rt_kprintf("Usage: hummingbird_panel_dma [status]\n");
        return -RT_EINVAL;
    }

    if (!device || rt_device_control(device, RTGRAPHIC_CTRL_GET_INFO, &info))
    {
        return -RT_ENOSYS;
    }

    if (physical >= 0x10000000UL || info.smem_len > 0x10000000UL - physical ||
        HWREG32(LS2K_UNCACHED(0x160915a0UL)) || info.bits_per_pixel != 32 ||
        info.pitch != info.width * 4)
    {
        return -RT_EINVAL;
    }

    cached = (rt_uint32_t *)info.framebuffer;
    uncached = (volatile rt_uint32_t *)LS2K_UNCACHED(physical);

    for (unsigned i = 0; !status_only && i < info.smem_len / sizeof(*cached); i++)
    {
        unsigned x = i % info.width;
        rt_uint32_t expected = x >= info.width / 3 && x < info.width * 2 / 3 ?
                               0xffff0000U : 0xffffffffU;

        if (cached[i] != expected || uncached[i] != expected)
        {
            memory_errors++;
        }

        if (!(i & 0x3fffU))
        {
            rt_thread_mdelay(1);
        }
    }

    if (!status_only)
    {
        rt_kprintf("LCD MEMORY bytes=%u mismatches=%u (CPU aliases only)\n",
                   (unsigned)info.smem_len, memory_errors);
    }
    rt_hw_interrupt_mask(51);
    saved_enable = HWREG32(status_reg) & 0xffff0000U;
    /* Some status sources are gated by their enable bits. Observe read-done,
     * underflow and fatal-underflow as well as vsync, without CPU IRQ delivery. */
    test_enable = saved_enable | (0x544U << 16);
    HWREG32(status_reg) = test_enable | 0xffffU;
    rt_hw_dmb();
    rt_kprintf("LCD DMA enable=%08x saved=%08x\n",
               HWREG32(status_reg) & 0xffff0000U, saved_enable);
    start = rt_tick_get();

    do
    {
        rt_uint32_t status = HWREG32(status_reg) & 0xffffU;

        observed |= status;
        frames += !!(status & (1U << 2));
        completed += !!(status & (1U << 6));
        underflow += !!(status & (1U << 8));
        fatal += !!(status & (1U << 10));
        anchored += !!(HWREG32(LS2K_UNCACHED(0x16091240UL)) & (1U << 24));
        HWREG32(status_reg) = test_enable | status;
        rt_hw_dmb();
        rt_thread_mdelay(1);
    } while (rt_tick_get() - start < rt_tick_from_millisecond(2000));

    HWREG32(status_reg) = saved_enable | 0xffffU;
    rt_hw_dmb();
    rt_hw_interrupt_umask(51);
    rt_kprintf("LCD DMA status=%04x vsync=%u read_done=%u underflow=%u fatal=%u anchored=%u\n",
               observed, frames, completed, underflow, fatal, anchored);
    rt_kprintf("LCD DMA counts are sampled status flags, not connector/optical verification\n");
    return memory_errors || !frames || underflow || fatal || anchored ? -RT_ERROR : RT_EOK;
}

static struct rt_timer test_guard;
static rt_tick_t guard_start;
static rt_tick_t guard_duration;
static rt_bool_t guard_initialized;

static void guard_tick(void *parameter)
{
    RT_UNUSED(parameter);

    if (rt_tick_get() - guard_start >= guard_duration)
    {
        /* DTS syscon-reboot: WD_EN bit 0 requests a platform reset. */
        HWREG32(LS2K_UNCACHED(0x16124000UL)) = 1;
        rt_hw_dmb();
        return;
    }

    HWREG32(LS2K_UNCACHED(0x16124004UL)) = 1;
    rt_hw_dmb();
}

static long hummingbird_test_guard(int argc, char **argv)
{
    unsigned seconds = argc == 2 ? strtoul(argv[1], RT_NULL, 10) : 0;

    if (!seconds || seconds > 1800)
    {
        return -RT_EINVAL;
    }

    if (guard_initialized)
    {
        rt_timer_stop(&test_guard);
    }
    else
    {
        rt_timer_init(&test_guard, "panel-guard", guard_tick, RT_NULL,
                      rt_tick_from_millisecond(1000),
                      RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
        guard_initialized = RT_TRUE;
    }

    guard_start = rt_tick_get();
    guard_duration = rt_tick_from_millisecond(seconds * 1000);

    /* Max hardware reload, fed once per second. No overflowing seconds*APB
     * calculation: the software deadline bounds the interactive test. */
    HWREG32(LS2K_UNCACHED(0x16124000UL)) = 0;
    HWREG32(LS2K_UNCACHED(0x16124008UL)) = 0xffffffffU;
    HWREG32(LS2K_UNCACHED(0x16124000UL)) = 2;
    HWREG32(LS2K_UNCACHED(0x16124004UL)) = 1;
    rt_hw_dmb();
    rt_timer_start(&test_guard);
    rt_kprintf("Hummingbird test guard: %u seconds, periodic hardware feed\n", seconds);
    return 0;
}

/* The pinned FinSH table stores parameterized commands as syscall_func. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
MSH_CMD_EXPORT(hummingbird_panel_dma, Sample DC DMA status [status skips known-bar check]);
MSH_CMD_EXPORT(hummingbird_panel_drive, Test DVO pad strength briefly then restore);
MSH_CMD_EXPORT(hummingbird_panel_probe, Blink and show opaque pattern [phase 0|1] [ddr]);
MSH_CMD_EXPORT(hummingbird_panel_signal, Test polarity mask and count vsync then restore);
MSH_CMD_EXPORT(hummingbird_test_guard, Keep diagnostic alive then reset [seconds 1..1800]);
#pragma GCC diagnostic pop
#endif
#endif
