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

#include "drv_i2c.h"
#include "loongson_board_info.h"

/* Debug configuration */
#define DBG_TAG "drv.i2c"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Get I2C base address from bus ID */
static inline rt_uint8_t *i2c_get_base(rt_uint8_t bus_id)
{
    switch (bus_id)
    {
    case 0:
        return (rt_uint8_t *)LS_I2C0_REG_BASE;
    case 1:
        return (rt_uint8_t *)LS_I2C1_REG_BASE;
    case 2:
        return (rt_uint8_t *)LS_I2C2_REG_BASE;
    case 3:
        return (rt_uint8_t *)LS_I2C3_REG_BASE;
    default:
        return 0;
    }
}

/* Wait for specific event with timeout */
static rt_err_t i2c_wait_ev_signal(struct ls2k300_i2c_bus *bus, rt_uint8_t reg_offset,
                                   rt_uint8_t bit_offset, rt_uint8_t target, rt_uint32_t timeout_ms)
{
    rt_uint32_t temp;
    rt_tick_t start_tick = rt_tick_get();
    rt_tick_t timeout_tick = rt_tick_from_millisecond(timeout_ms);

    while (1)
    {
        temp = readl(bus->base_addr + reg_offset);
        temp &= (1 << bit_offset);

        if (!!(temp) == !!(target))
        {
            return RT_EOK;
        }

        if ((rt_tick_get() - start_tick) > timeout_tick)
        {
            LOG_E("I2C%d timeout waiting for bit %d in reg 0x%x to be %d", bus->bus_id, bit_offset,
                  reg_offset, target);
            return -RT_ETIMEOUT;
        }

        // rt_thread_mdelay(1);
    }
}

/* Convenience macro for wait with default timeout */
#define i2c_wait_signal(bus, reg, bit, target) i2c_wait_ev_signal(bus, reg, bit, target, 100)

/* Initialize I2C clock configuration */
static void i2c_init_clock(struct ls2k300_i2c_bus *bus)
{
    rt_uint32_t temp;

    /* Configure CR2: Set frequency mask */
    temp = readl(bus->base_addr + I2C_CR2_OFFSET);
    temp |= I2C_CR2_FREQ_MASK;
    writel(temp, bus->base_addr + I2C_CR2_OFFSET);

    /* Configure TRISE: Set max rise time */
    temp = readl(bus->base_addr + I2C_TRISE_OFFSET);
    temp |= I2C_TRISE_TRISE_MASK;
    writel(temp, bus->base_addr + I2C_TRISE_OFFSET);

    /* Configure CCR */
    temp = readl(bus->base_addr + I2C_CCR_OFFSET);

    /* Set Fast Mode (400KHz) or Standard Mode (100KHz) */
    temp |= (1 << I2C_CCR_F_S);
    if (bus->speed_mode == LS2K300_I2C_SPEED_100K)
    {
        temp ^= (1 << I2C_CCR_F_S);  /* Clear for 100KHz */
    }
    /* 400K 1:2 100K 1:1*/
    temp |= (1 << I2C_CCR_DUTY);
    temp ^= (1 << I2C_CCR_DUTY);

    /* Clear and set CCR value */
    temp |= I2C_CCR_CCR_MASK;
    temp ^= I2C_CCR_CCR_MASK;
    temp |= (bus->speed_mode == LS2K300_I2C_SPEED_400K) ? CCR_400KHZ : CCR_100KHZ;

    /* Configure duty cycle */
    temp |= (1 << I2C_CCR_DUTY);
    temp ^= (1 << I2C_CCR_DUTY);

    writel(temp, bus->base_addr + I2C_CCR_OFFSET);
}

/* Initialize I2C controller */
static rt_err_t i2c_controller_init(struct ls2k300_i2c_bus *bus)
{
    rt_uint32_t temp;

    /* Get base address */
    bus->base_addr = i2c_get_base(bus->bus_id);
    if (bus->base_addr == 0)
    {
        LOG_E("Invalid I2C bus ID: %d", bus->bus_id);
        return -RT_EINVAL;
    }

    /* Initialize clock */
    i2c_init_clock(bus);

    /* Enable I2C peripheral */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= (1 << I2C_CR1_PE);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    LOG_D("I2C%d initialized at 0x%08X, speed: %s", bus->bus_id, bus->base_addr,
          bus->speed_mode == LS2K300_I2C_SPEED_400K ? "400KHz" : "100KHz");

    return RT_EOK;
}

/* Reset I2C bus on error */
static void i2c_bus_error_reset(struct ls2k300_i2c_bus *bus)
{
    rt_uint32_t temp;
    rt_uint32_t temp_sr1;
    rt_uint32_t temp_cr1_mask = 0;

    temp_sr1 = readl(bus->base_addr + I2C_SR1_OFFSET);
    if (temp_sr1 & (1 << I2C_SR1_BERR))
    {
        temp_cr1_mask = (1 << I2C_CR1_RECOVER);
    }

    /* Software reset */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= ((1 << I2C_CR1_SWRST) | temp_cr1_mask);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    /* Clear errors */
    temp = readl(bus->base_addr + I2C_SR1_OFFSET);
    temp |= (0xf << 8);
    temp ^= (0xf << 8);
    writel(temp, bus->base_addr + I2C_SR1_OFFSET);
    (void)readl(bus->base_addr + I2C_SR1_OFFSET);  /* Read to clear */

    /* Release reset */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= ((1 << I2C_CR1_SWRST) | temp_cr1_mask);
    temp ^= ((1 << I2C_CR1_SWRST) | temp_cr1_mask);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    /* Re-enable peripheral */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= (1 << I2C_CR1_PE);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    LOG_W("I2C%d bus reset completed", bus->bus_id);
}

/* Set STOP and ACK bits for read operations */
static inline void i2c_cr1_stop_set(struct ls2k300_i2c_bus *bus, rt_uint8_t stop)
{
    rt_uint32_t temp;

    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= ((1 << I2C_CR1_ACK) | (1 << I2C_CR1_POS));
    temp ^= ((1 << I2C_CR1_ACK) | (1 << I2C_CR1_POS));
    temp |= (stop << I2C_CR1_STOP);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);
}

/* Read 1 byte */
static rt_err_t i2c_read_1byte(struct ls2k300_i2c_bus *bus, struct rt_i2c_msg *msg)
{
    rt_err_t ret;

    i2c_cr1_stop_set(bus, msg->flags & RT_I2C_NO_STOP ? 0 : 1);

    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_RXNE, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[0] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    return RT_EOK;
}

/* Read 2 bytes */
static rt_err_t i2c_read_2bytes(struct ls2k300_i2c_bus *bus, struct rt_i2c_msg *msg)
{
    rt_err_t ret;

    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_RXNE, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[0] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    i2c_cr1_stop_set(bus, msg->flags & RT_I2C_NO_STOP ? 0 : 1);

    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_RXNE, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[1] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    return RT_EOK;
}

/* Read 3 or more bytes */
static rt_err_t i2c_read_3bytes_plus(struct ls2k300_i2c_bus *bus, struct rt_i2c_msg *msg)
{
    rt_err_t ret;
    int i = 0;
    int remain_read = msg->len;

    /* Read until last 3 bytes */
    while (remain_read > 3)
    {
        ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_RXNE, 1);
        if (ret != RT_EOK)
        {
            return ret;
        }

        msg->buf[i++] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);
        remain_read--;
    }

    /* Handle last 3 bytes according to LS2K300 manual */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_BTF, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[i++] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    i2c_cr1_stop_set(bus, msg->flags & RT_I2C_NO_STOP ? 0 : 1);

    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_BTF, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[i++] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_RXNE, 1);
    if (ret != RT_EOK)
    {
        return ret;
    }

    msg->buf[i++] = (rt_uint8_t)(readl(bus->base_addr + I2C_DR_OFFSET) & 0xff);

    return RT_EOK;
}

/* I2C read transfer */
static rt_err_t i2c_xfer_read(struct ls2k300_i2c_bus *bus, struct rt_i2c_msg *msg)
{
    rt_err_t ret;
    rt_uint32_t temp;
    rt_uint8_t xfer_data;

    /* Send START and enable ACK */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= (1 << I2C_CR1_START) | (1 << I2C_CR1_ACK);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    /* Wait for START generated */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_SB, 1);
    if (ret != RT_EOK)
    {
        LOG_E("I2C%d: Timeout waiting for SB", bus->bus_id);
        goto xfer_read_err;
    }

    /* Send slave address with read bit */
    xfer_data = (msg->addr << 1) | 0x01;  /* Address + Read bit */
    writeb(xfer_data, bus->base_addr + I2C_DR_OFFSET);

    /* Wait for address sent */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_ADDR, 1);
    if (ret != RT_EOK)
    {
        LOG_E("I2C%d: Timeout waiting for ADDR (0x%02X)", bus->bus_id, msg->addr);
        goto xfer_read_err;
    }

    /* Clear ADDR by reading SR2 */
    temp = readl(bus->base_addr + I2C_SR2_OFFSET);

    /* Read data based on length */
    if (msg->len == 1)
    {
        ret = i2c_read_1byte(bus, msg);
    }
    else if (msg->len == 2)
    {
        ret = i2c_read_2bytes(bus, msg);
    }
    else if (msg->len >= 3)
    {
        ret = i2c_read_3bytes_plus(bus, msg);
    }

xfer_read_err:
    if (ret != RT_EOK)
    {
        i2c_bus_error_reset(bus);
    }
    else if (!(msg->flags & RT_I2C_NO_STOP))
    {
        /* Wait for BUSY to clear */
        (void)i2c_wait_signal(bus, I2C_SR2_OFFSET, I2C_SR2_BUSY, 0);
    }

    return ret;
}

/* I2C write transfer */
static rt_err_t i2c_xfer_write(struct ls2k300_i2c_bus *bus, struct rt_i2c_msg *msg)
{
    rt_err_t ret;
    rt_uint32_t temp;
    rt_uint8_t xfer_data;
    int i;

    /* Send START */
    temp = readl(bus->base_addr + I2C_CR1_OFFSET);
    temp |= (1 << I2C_CR1_START);
    writel(temp, bus->base_addr + I2C_CR1_OFFSET);

    /* Wait for START generated */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_SB, 1);
    if (ret != RT_EOK)
    {
        LOG_E("I2C%d: Timeout waiting for SB", bus->bus_id);
        goto xfer_write_err;
    }

    /* Send slave address with write bit */
    xfer_data = (msg->addr << 1);  /* Address + Write bit (0) */
    writeb(xfer_data, bus->base_addr + I2C_DR_OFFSET);

    /* Wait for address sent */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_ADDR, 1);
    if (ret != RT_EOK)
    {
        LOG_E("I2C%d: Timeout waiting for ADDR (0x%02X)", bus->bus_id, msg->addr);
        goto xfer_write_err;
    }

    /* Clear ADDR by reading SR2 */
    temp = readl(bus->base_addr + I2C_SR2_OFFSET);

    /* Wait for TXE */
    ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_TXE, 1);
    if (ret != RT_EOK)
    {
        LOG_E("I2C%d: Timeout waiting for TXE", bus->bus_id);
        goto xfer_write_err;
    }

    /* Send data */
    for (i = 0; i < msg->len; i++)
    {
        writeb(msg->buf[i], bus->base_addr + I2C_DR_OFFSET);

        /* Wait for transfer complete */
        ret = i2c_wait_signal(bus, I2C_SR1_OFFSET, I2C_SR1_BTF, 1);
        if (ret != RT_EOK)
        {
            LOG_E("I2C%d: Timeout waiting for BTF at byte %d", bus->bus_id, i);
            goto xfer_write_err;
        }
    }

xfer_write_err:
    if (ret != RT_EOK)
    {
        i2c_bus_error_reset(bus);
    }
    else
    {
        if (!(msg->flags & RT_I2C_NO_STOP))
        {
            /* Send STOP */
            temp = readl(bus->base_addr + I2C_CR1_OFFSET);
            temp |= (1 << I2C_CR1_STOP);
            writel(temp, bus->base_addr + I2C_CR1_OFFSET);

            /* Wait for BUSY to clear */
            (void)i2c_wait_signal(bus, I2C_SR2_OFFSET, I2C_SR2_BUSY, 0);
        }
    }

    return ret;
}

/* RT-Thread I2C master transfer function */
static rt_ssize_t ls2k300_i2c_master_xfer(struct rt_i2c_bus_device *bus, struct rt_i2c_msg msgs[],
                                          rt_uint32_t num)
{
    struct ls2k300_i2c_bus *ls2k300_bus = (struct ls2k300_i2c_bus *)bus;
    rt_err_t ret = RT_EOK;
    rt_uint32_t i;

    RT_ASSERT(bus != RT_NULL);
    RT_ASSERT(msgs != RT_NULL);

    /* Take mutex lock */
    ret = rt_mutex_take(&ls2k300_bus->lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK)
    {
        return ret;
    }

    for (i = 0; i < num; i++)
    {
        struct rt_i2c_msg *msg = &msgs[i];

        if (msg->flags & RT_I2C_RD)
        {
            ret = i2c_xfer_read(ls2k300_bus, msg);
        }
        else
        {
            ret = i2c_xfer_write(ls2k300_bus, msg);
        }

        if (ret != RT_EOK)
        {
            LOG_E("I2C%d: Transfer failed at message %d, ret=%d", ls2k300_bus->bus_id, i, ret);
            break;
        }

        /* Delay between messages if specified */
        if (msg->len > 0 && i < (num - 1))
        {
            /* Small delay between consecutive transfers */
            rt_thread_delay(1);
        }
    }

    rt_mutex_release(&ls2k300_bus->lock);

    return (ret == RT_EOK) ? (rt_ssize_t)i : (rt_ssize_t)ret;
}

/* I2C bus control function */
static rt_err_t ls2k300_i2c_bus_control(struct rt_i2c_bus_device *bus, int cmd, void *arg)
{
    struct ls2k300_i2c_bus *ls2k300_bus = (struct ls2k300_i2c_bus *)bus;

    RT_ASSERT(bus != RT_NULL);

    switch (cmd)
    {
    case RT_I2C_DEV_CTRL_CLK:
        /* Set clock speed */
        if (arg && *(rt_uint32_t *)arg == 400000)
        {
            ls2k300_bus->speed_mode = LS2K300_I2C_SPEED_400K;
        }
        else
        {
            ls2k300_bus->speed_mode = LS2K300_I2C_SPEED_100K;
        }
        i2c_init_clock(ls2k300_bus);
        return RT_EOK;

    case RT_I2C_DEV_CTRL_TIMEOUT:
        /* Set timeout */
        if (!arg)
        {
            return -RT_EINVAL;
        }
        bus->timeout = *(rt_uint32_t *)arg;
        return RT_EOK;

    default:
        return -RT_EINVAL;
    }
}

/* I2C bus device operations */
static const struct rt_i2c_bus_device_ops ls2k300_i2c_ops = {
    .master_xfer = ls2k300_i2c_master_xfer,
    .slave_xfer = RT_NULL,  /* Slave mode not supported */
    .i2c_bus_control = ls2k300_i2c_bus_control,
};

#ifdef BSP_USING_I2C0
static struct ls2k300_i2c_bus i2c0_bus = {
    .bus_id = 0,
    .speed_mode = LS2K300_I2C_SPEED_400K,
};
#endif

#ifdef BSP_USING_I2C1
static struct ls2k300_i2c_bus i2c1_bus = {
    .bus_id = 1,
    .speed_mode = LS2K300_I2C_SPEED_400K,
};
#endif

#ifdef BSP_USING_I2C2
static struct ls2k300_i2c_bus i2c2_bus = {
    .bus_id = 2,
    .speed_mode = LS2K300_I2C_SPEED_400K,
};
#endif

#ifdef BSP_USING_I2C3
static struct ls2k300_i2c_bus i2c3_bus = {
    .bus_id = 3,
    .speed_mode = LS2K300_I2C_SPEED_400K,
};
#endif

static int ls2k300_i2c_bus_register(struct ls2k300_i2c_bus *bus, const char *name)
{
    rt_err_t ret;

    /* Initialize mutex */
    ret = rt_mutex_init(&bus->lock, name, RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to init mutex for %s", name);
        return ret;
    }

    /* Initialize I2C controller */
    ret = i2c_controller_init(bus);
    if (ret != RT_EOK)
    {
        rt_mutex_detach(&bus->lock);
        return ret;
    }

    /* Setup RT-Thread I2C bus device */
    bus->parent.ops = &ls2k300_i2c_ops;
    bus->parent.timeout = LS2K300_I2C_TIMEOUT;
    bus->parent.retries = 3;

    /* Register to RT-Thread I2C bus device */
    ret = rt_i2c_bus_device_register(&bus->parent, name);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to register I2C bus %s", name);
        rt_mutex_detach(&bus->lock);
        return ret;
    }

    LOG_I("I2C bus %s registered successfully", name);
    return RT_EOK;
}

int rt_hw_i2c_init(void)
{
    rt_err_t ret = RT_EOK;

#ifdef BSP_USING_I2C0
    ret = ls2k300_i2c_bus_register(&i2c0_bus, I2C0_BUS_NAME);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to init I2C0");
    }
#endif

#ifdef BSP_USING_I2C1
    ret = ls2k300_i2c_bus_register(&i2c1_bus, I2C1_BUS_NAME);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to init I2C1");
    }
#endif

#ifdef BSP_USING_I2C2
    ret = ls2k300_i2c_bus_register(&i2c2_bus, I2C2_BUS_NAME);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to init I2C2");
    }
#endif

#ifdef BSP_USING_I2C3
    ret = ls2k300_i2c_bus_register(&i2c3_bus, I2C3_BUS_NAME);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to init I2C3");
    }
#endif

    return ret;
}

INIT_DEVICE_EXPORT(rt_hw_i2c_init);
