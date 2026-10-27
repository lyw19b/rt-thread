/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     OpenLANPU Contributors    adapt Vanguard Pi I2C0 touch wiring
 *
 * Description:
 *   Bind the GT9147 described by ls2k300_vanguard_pi_common.dtsi.
 * Notes:
 *   Linux GPIO virtual IRQ170 is not a CPU interrupt number. GPIO42 belongs
 *   to the GPIO32..47 bank on LIOINTC1 input23, or RT-Thread IRQ55. This
 *   adapter owns that bank handler; future GPIO users need a shared demux.
 */

#include <rthw.h>
#include "ls2k.h"
#include "platform_interrupt.h"

#ifdef BSP_USING_VANGUARD_TOUCH
#include <drivers/dev_pin.h>
#include <ls2k0300_display.h>

#define TOUCH_GPIO_BASE 0x16104000UL
#define TOUCH_PINMUX_BASE 0x16000490UL
#define TOUCH_INT_PIN 42
#define TOUCH_RESET_PIN 55
#define TOUCH_BANK_IRQ 55
#define GPIO_DIR 0x800
#define GPIO_OUT 0x900
#define GPIO_IRQ_ENABLE 0xb00
#define GPIO_IRQ_POLARITY 0xc00
#define GPIO_IRQ_EDGE 0xd00
#define GPIO_IRQ_CLEAR 0xe00
#define GPIO_IRQ_STATUS 0xf00
#define GPIO_IRQ_BOTH 0xf80

static void gpio_write(unsigned int reg, unsigned int pin, rt_uint8_t value)
{
    HWREG8(LS2K_UNCACHED(TOUCH_GPIO_BASE + reg + pin)) = value;
    rt_hw_dmb();
}

static void gpio_output(unsigned int pin, rt_uint8_t value)
{
    gpio_write(GPIO_OUT, pin, value);
    gpio_write(GPIO_DIR, pin, 0);
}

static void touch_mask(int irq)
{
    (void)irq;
    gpio_write(GPIO_IRQ_ENABLE, TOUCH_INT_PIN, 0);
}

static void touch_unmask(int irq)
{
    gpio_write(GPIO_IRQ_ENABLE, TOUCH_INT_PIN, 1);
    rt_hw_interrupt_umask(irq);
}

static rt_bool_t touch_pending_ack(int irq)
{
    (void)irq;

    if (!(HWREG8(LS2K_UNCACHED(TOUCH_GPIO_BASE + GPIO_IRQ_ENABLE + TOUCH_INT_PIN)) & 1) ||
        !(HWREG8(LS2K_UNCACHED(TOUCH_GPIO_BASE + GPIO_IRQ_STATUS + TOUCH_INT_PIN)) & 1))
    {
        return RT_FALSE;
    }

    gpio_write(GPIO_IRQ_CLEAR, TOUCH_INT_PIN, 1);
    return RT_TRUE;
}

static const struct laxpu_touch_irq_ops touch_irq_ops = {
    .mask = touch_mask,
    .unmask = touch_unmask,
    .pending_ack = touch_pending_ack,
};

static void touch_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_ubase_t mux = LS2K_UNCACHED(TOUCH_PINMUX_BASE);

    /* G42 is GPIO (not UART1); G55 is GPIO (not I2C3); G48/G49 are I2C0.
     * Preserve every unrelated pin, including G43 LCD backlight. */
    HWREG32(mux + 0x8) &= ~(3U << 20);
    HWREG32(mux + 0xc) = (HWREG32(mux + 0xc) & ~0xc00fU) | 0xfU;
    rt_hw_dmb();
    rt_hw_interrupt_enable(level);

    touch_mask(TOUCH_BANK_IRQ);
    gpio_write(GPIO_IRQ_ENABLE, TOUCH_RESET_PIN, 0);
    gpio_output(TOUCH_RESET_PIN, 0);
    rt_thread_mdelay(20);

    /* INT high at RESET release selects the seven-bit I2C address 0x14. */
    gpio_output(TOUCH_INT_PIN, 1);
    rt_thread_mdelay(1);
    gpio_output(TOUCH_RESET_PIN, 1);
    rt_thread_mdelay(6);
    gpio_write(GPIO_DIR, TOUCH_RESET_PIN, 1);

    gpio_output(TOUCH_INT_PIN, 0);
    rt_thread_mdelay(50);
    gpio_write(GPIO_DIR, TOUCH_INT_PIN, 1);
    gpio_write(GPIO_IRQ_BOTH, TOUCH_INT_PIN, 0);
    gpio_write(GPIO_IRQ_POLARITY, TOUCH_INT_PIN, 0);
    gpio_write(GPIO_IRQ_EDGE, TOUCH_INT_PIN, 1);
    gpio_write(GPIO_IRQ_CLEAR, TOUCH_INT_PIN, 1);
}

int ls2k0300_vanguard_touch_init(void)
{
    int error;

    touch_reset();
    error = ls2k0300_i2c_register((void *)LS2K_UNCACHED(0x16108000UL), 200000000, "i2c0");
    if (!error)
    {
        /* The GPIO edge latch drives a level-high parent interrupt. */
        liointc_set_irq_mode(TOUCH_BANK_IRQ, PIN_IRQ_MODE_HIGH_LEVEL);
        error = laxpu_gt9147_register("i2c0", 0x14, TOUCH_BANK_IRQ, &touch_irq_ops);
    }

    if (error)
    {
        touch_mask(TOUCH_BANK_IRQ);
        rt_kprintf("Vanguard touch: I2C0 GT9147@0x14 probe failed (%d)\n", error);
    }

    return error;
}
#endif
