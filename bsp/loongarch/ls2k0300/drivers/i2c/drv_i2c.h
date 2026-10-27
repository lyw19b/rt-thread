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
 *   LS2K0300 development BSP: drv i2c.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_I2C_H__
#define __DRV_I2C_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "loongson_soc_info.h"

// /* I2C Reference Clock (APB clock) */
// #ifndef I2C_REF_CLK
// #define I2C_REF_CLK             (100000000)  /* 100MHz APB clock, adjust according to actual hardware */
// #endif

/* I2C Speed Mode */
#define LS2K300_I2C_SPEED_100K 0
#define LS2K300_I2C_SPEED_400K 1

/* I2C Register Offsets */
#define I2C_CR1_OFFSET   0x00
#define I2C_CR2_OFFSET   0x04
#define I2C_OAR_OFFSET   0x08
#define I2C_DR_OFFSET    0x10
#define I2C_SR1_OFFSET   0x14
#define I2C_SR2_OFFSET   0x18
#define I2C_CCR_OFFSET   0x1c
#define I2C_TRISE_OFFSET 0x20

/* I2C_CR1 Bits */
#define I2C_CR1_PE      (0)
#define I2C_CR1_START   (8)
#define I2C_CR1_STOP    (9)
#define I2C_CR1_ACK     (10)
#define I2C_CR1_POS     (11)
#define I2C_CR1_RECOVER (14)
#define I2C_CR1_SWRST   (15)

/* I2C_SR1 Bits */
#define I2C_SR1_SB   (0)
#define I2C_SR1_ADDR (1)
#define I2C_SR1_BTF  (2)
#define I2C_SR1_RXNE (6)
#define I2C_SR1_TXE  (7)
#define I2C_SR1_BERR (8)
#define I2C_SR1_ARLO (9)
#define I2C_SR1_AF   (10)

/* I2C_SR2 Bits */
#define I2C_SR2_BUSY (1)

/* I2C_CCR Bits */
#define I2C_CCR_CCR_MASK (0xfff)
#define I2C_CCR_DUTY     (14)
#define I2C_CCR_F_S      (15)

/* I2C_TRISE Mask */
#define I2C_TRISE_TRISE_MASK (0x3f)

/* I2C_CR2 Mask */
#define I2C_CR2_FREQ_MASK (0x3f)

/* CCR Calculation Macros */
#define CCR_100KHZ (I2C_REF_CLK / (100000 * 2))
#define CCR_400KHZ (I2C_REF_CLK / (400000 * 3))

/* Timeout definitions (in ticks) */
#define LS2K300_I2C_TIMEOUT (RT_TICK_PER_SECOND / 10) /* 100ms timeout */

/* I2C Bus Device Structure */
struct ls2k300_i2c_bus
{
    struct rt_i2c_bus_device parent;
    rt_uint8_t *base_addr;
    rt_uint8_t bus_id;
    rt_uint8_t speed_mode;
    struct rt_mutex lock;
};

/* Board Configuration - define these in board.h */
#ifdef BSP_USING_I2C0
#define I2C0_BUS_NAME "i2c0"
#endif
#ifdef BSP_USING_I2C1
#define I2C1_BUS_NAME "i2c1"
#endif
#ifdef BSP_USING_I2C2
#define I2C2_BUS_NAME "i2c2"
#endif
#ifdef BSP_USING_I2C3
#define I2C3_BUS_NAME "i2c3"
#endif

int rt_hw_i2c_init(void);

#endif /* __DRV_I2C_H__ */
