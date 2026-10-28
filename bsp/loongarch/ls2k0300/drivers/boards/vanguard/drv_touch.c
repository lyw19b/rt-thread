/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     LoongsonLab               adapt Vanguard Pi I2C0 touch wiring
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 * 2026-10-28     LoongsonLab               release an interrupted touch I2C transaction
 */

/*
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

#define TOUCH_GPIO_BASE   0x16104000UL
#define TOUCH_PINMUX_BASE 0x16000490UL
#define TOUCH_INT_PIN     42
#define TOUCH_RESET_PIN   55
#define TOUCH_BANK_IRQ    55
#define TOUCH_SCL_PIN     48
#define TOUCH_SDA_PIN     49
#define TOUCH_I2C_BASE    0x16108000UL
#define GPIO_DIR          0x800
#define GPIO_OUT          0x900
#define GPIO_IN           0xa00
#define GPIO_IRQ_ENABLE   0xb00
#define GPIO_IRQ_POLARITY 0xc00
#define GPIO_IRQ_EDGE     0xd00
#define GPIO_IRQ_CLEAR    0xe00
#define GPIO_IRQ_STATUS   0xf00
#define GPIO_IRQ_BOTH     0xf80

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

static rt_bool_t gpio_is_high(unsigned int pin)
{
    return (HWREG8(LS2K_UNCACHED(TOUCH_GPIO_BASE + GPIO_IN + pin)) & 1) != 0;
}

static int touch_wait_scl(void)
{
    for (unsigned int attempt = 0; attempt < 10; ++attempt)
    {
        if (gpio_is_high(TOUCH_SCL_PIN))
        {
            return RT_EOK;
        }

        rt_thread_mdelay(1);
    }

    return -RT_ETIMEOUT;
}

/* A warm reset can interrupt an I2C read with the panel still holding SDA low.
 * Resetting only the host does not clock that byte out. At board takeover,
 * emulate open-drain GPIO: drive low or release to input, never drive high.
 * Nine bounded clocks and STOP recover a slave waiting for its last ACK.
 * This runs before bus registration, with no live I2C clients. */
static int touch_release_bus(void)
{
    rt_ubase_t mux = LS2K_UNCACHED(TOUCH_PINMUX_BASE + 0xc);
    rt_ubase_t gpio = LS2K_UNCACHED(TOUCH_GPIO_BASE);
    rt_uint32_t saved_mux = HWREG32(mux);
    rt_uint8_t scl_dir = HWREG8(gpio + GPIO_DIR + TOUCH_SCL_PIN);
    rt_uint8_t sda_dir = HWREG8(gpio + GPIO_DIR + TOUCH_SDA_PIN);
    rt_uint8_t scl_out = HWREG8(gpio + GPIO_OUT + TOUCH_SCL_PIN);
    rt_uint8_t sda_out = HWREG8(gpio + GPIO_OUT + TOUCH_SDA_PIN);
    unsigned int clocks = 0;
    int error;

    HWREG32(LS2K_UNCACHED(TOUCH_I2C_BASE)) = 0;
    rt_hw_dmb();
    gpio_write(GPIO_DIR, TOUCH_SCL_PIN, 1);
    gpio_write(GPIO_DIR, TOUCH_SDA_PIN, 1);
    gpio_write(GPIO_OUT, TOUCH_SCL_PIN, 0);
    gpio_write(GPIO_OUT, TOUCH_SDA_PIN, 0);
    HWREG32(mux) = saved_mux & ~15U;
    rt_hw_dmb();
    rt_thread_mdelay(1);

    error = touch_wait_scl();

    while (!error && !gpio_is_high(TOUCH_SDA_PIN) && clocks < 9)
    {
        gpio_write(GPIO_DIR, TOUCH_SCL_PIN, 0);
        rt_thread_mdelay(1);
        gpio_write(GPIO_DIR, TOUCH_SCL_PIN, 1);
        rt_thread_mdelay(1);
        error = touch_wait_scl();
        ++clocks;
    }

    if (!error && clocks)
    {
        gpio_write(GPIO_DIR, TOUCH_SCL_PIN, 0);
        gpio_write(GPIO_DIR, TOUCH_SDA_PIN, 0);
        rt_thread_mdelay(1);
        gpio_write(GPIO_DIR, TOUCH_SCL_PIN, 1);
        rt_thread_mdelay(1);
        error = touch_wait_scl();
        gpio_write(GPIO_DIR, TOUCH_SDA_PIN, 1);
        rt_thread_mdelay(1);
    }

    if (!error && !gpio_is_high(TOUCH_SDA_PIN))
    {
        error = -RT_EIO;
    }

    /* Restore pin ownership even if a line is externally held low. */
    HWREG32(mux) = saved_mux;
    rt_hw_dmb();
    gpio_write(GPIO_OUT, TOUCH_SCL_PIN, scl_out);
    gpio_write(GPIO_OUT, TOUCH_SDA_PIN, sda_out);
    gpio_write(GPIO_DIR, TOUCH_SCL_PIN, scl_dir);
    gpio_write(GPIO_DIR, TOUCH_SDA_PIN, sda_dir);

    if (error)
    {
        rt_kprintf("Vanguard touch: I2C0 bus release failed (%d)\n", error);
    }
    else if (clocks)
    {
        rt_kprintf("Vanguard touch: I2C0 recovered after %u clocks and STOP\n", clocks);
    }

    return error;
}

int ls2k0300_vanguard_touch_init(void)
{
    int error;

    touch_reset();
    error = touch_release_bus();

    if (!error)
    {
        error = ls2k0300_i2c_register((void *)LS2K_UNCACHED(TOUCH_I2C_BASE),
                                     200000000, "i2c0");
    }

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
