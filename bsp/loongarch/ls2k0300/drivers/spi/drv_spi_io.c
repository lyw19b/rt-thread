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
 *   LS2K0300 development BSP: drv spi io.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include "drv_spi_io.h"
#include "loongson_soc_info.h"
#include "bsp_dma.h"
#include "bsp_dma_map.h"

#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "drv.spi"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define MIN_SPI_NUM        2
#define MAX_SPI_NUM        3

/* SPI bus device structure */
struct ls2k300_spi
{
    struct rt_spi_bus parent;
    rt_uint8_t spi_num;
    rt_uint32_t ref_clk;
    rt_uint32_t max_speed_hz;
    rt_uint32_t current_speed;

    /* DMA support */
    rt_uint8_t dma_support;
    struct rt_completion transfer_done;
    dma_transfer_info tx_dma;
    dma_transfer_info rx_dma;
};

/* SPI CS pin control structure - compatible with rt_spi_bus_attach_device_cspin */
struct ls2k300_spi_cs
{
    rt_base_t pin;        /* CS pin number, -1 for hardware CS */
    rt_bool_t active_low; /* CS active level: RT_TRUE for active low */
};

static struct ls2k300_spi spi_bus[2];   /* SPI2 and SPI3 */

static inline rt_uint8_t *spi_io_get_reg_base(rt_uint8_t spi_num)
{
    rt_uint8_t *res;

    if (spi_num > MAX_SPI_NUM || spi_num < MIN_SPI_NUM)
    {
        return RT_NULL;
    }

    res = (rt_uint8_t *)LS_SPI2_BASE;
    res += ((spi_num - MIN_SPI_NUM) * 0x2000);
    return res;
}

static inline int spi_io_wait_sr_event(rt_uint8_t *base, rt_uint8_t offset, rt_uint32_t target,
                                       rt_uint32_t timeout_us)
{
    rt_uint32_t val;
    rt_uint32_t count = 0;

    while (1)
    {
        val = readl(base + offset);
        if ((val & target) == target)
        {
            break;
        }

        if (++count >= timeout_us)
        {
            return -RT_ETIMEOUT;
        }
        rt_hw_us_delay(1);
    }
    return RT_EOK;
}

static void ls2k300_spi_io_dma_callback(dma_transfer_info *dma_info, int status)
{
    struct ls2k300_spi *spi = (struct ls2k300_spi *)dma_info->dma_callback_param;
    rt_completion_done(&spi->transfer_done);
}

static rt_err_t ls2k300_spi_io_configure(struct rt_spi_device *device,
                                         struct rt_spi_configuration *configuration)
{
    struct ls2k300_spi *spi = (struct ls2k300_spi *)device->bus;
    rt_uint8_t *spi_io_base = spi_io_get_reg_base(spi->spi_num);
    rt_uint32_t val_1 = 0, val_2 = 0;
    rt_uint32_t div;

    RT_ASSERT(spi_io_base != RT_NULL);

    /* The PIO transfer loop operates on bytes only. */
    if (!configuration->max_hz || configuration->data_width != 8 ||
        (configuration->mode & (RT_SPI_SLAVE | RT_SPI_LSB)) || !SPI_IO_REF_CLK ||
        configuration->max_hz < DIV_ROUND_UP(SPI_IO_REF_CLK, 255))
    {
        return -RT_EINVAL;
    }

    /* Disable SPI first */
    val_1 = readl(spi_io_base + REG_CR1);
    val_1 &= ~BIT_CR1_SPE;
    writel(val_1, spi_io_base + REG_CR1);

    /* Calculate clock divider */
    div = DIV_ROUND_UP(SPI_IO_REF_CLK, configuration->max_hz);
    if (div < 2)
    {
        div = 2;
    }
    if (div > 255)
    {
        div = 255;
    }

    /* Set clock */
    val_2 = readl(spi_io_base + REG_CFG2);
    val_2 &= ~(0xff << 8);
    val_2 |= ((div & 0xff) << 8);
    writel(val_2, spi_io_base + REG_CFG2);

    /* Set mode (CPOL, CPHA, data size) */
    val_1 = 0;
    if (configuration->mode & RT_SPI_CPOL)
    {
        val_1 |= BIT_CFG1_CPOL;
    }
    if (configuration->mode & RT_SPI_CPHA)
    {
        val_1 |= BIT_CFG1_CPHA;
    }

    /* Data size: default 8 bits */
    rt_uint8_t data_size = 8;
    if (configuration->data_width <= 8)
    {
        data_size = 8;
    }
    else if (configuration->data_width <= 16)
    {
        data_size = 16;
    }
    else
    {
        data_size = 32;
    }

    val_1 |= ((data_size - 1) << 8);
    writel(val_1, spi_io_base + REG_CFG1);

    /* Configure as master mode */
    val_1 = readl(spi_io_base + REG_CFG3);
    val_1 = BIT_CFG3_MSTR | BIT_CFG3_DIE | BIT_CFG3_DOE;
    writel(val_1, spi_io_base + REG_CFG3);

    /* Enable SPI */
    val_1 = readl(spi_io_base + REG_CR1);
    val_1 |= BIT_CR1_SPE | BIT_CR1_SSREV;
    writel(val_1, spi_io_base + REG_CR1);

    spi->current_speed = configuration->max_hz;

    LOG_D("SPI%d configured: speed=%d, mode=0x%x, bits=%d", spi->spi_num, configuration->max_hz,
          configuration->mode, data_size);

    return RT_EOK;
}

static void ls2k300_spi_io_cs_control(struct rt_spi_device *device, rt_bool_t enable)
{
    struct ls2k300_spi_cs *cs = (struct ls2k300_spi_cs *)device->parent.user_data;
    struct ls2k300_spi *spi = (struct ls2k300_spi *)device->bus;
    rt_uint8_t *spi_base = spi_io_get_reg_base(spi->spi_num);
    rt_uint32_t val;

    if (cs == RT_NULL)
    {
        return;
    }

    if (cs->pin >= 0)
    {
        /* Use GPIO for chip select */
        rt_pin_write(cs->pin, enable ? (cs->active_low ? PIN_LOW : PIN_HIGH)
                                     : (cs->active_low ? PIN_HIGH : PIN_LOW));
    }
    else
    {
        /* Use SPI controller hardware CS */
        val = readl(spi_base + REG_CR1);
        if (enable)
        {
            /* CS active (low if active_low) */
            if (cs->active_low)
            {
                val |= (BIT_CR1_SSREV | BIT_CR1_SPE);
                val &= ~BIT_CR1_SSREV;  /* CS = !SSREV = 0 */
            }
            else
            {
                val |= (BIT_CR1_SSREV | BIT_CR1_SPE);  /* CS = SSREV = 1 */
            }
        }
        else
        {
            /* CS inactive */
            if (cs->active_low)
            {
                val |= (BIT_CR1_SSREV | BIT_CR1_SPE);  /* CS = SSREV = 1 */
            }
            else
            {
                val |= (BIT_CR1_SSREV | BIT_CR1_SPE);
                val &= ~BIT_CR1_SSREV;  /* CS = !SSREV = 0 */
            }
        }
        writel(val, spi_base + REG_CR1);
    }

    /* When CS is released, disable SPI state machine to prevent errors */
    if (!enable)
    {
        val = readl(spi_base + REG_CR1);
        val &= ~BIT_CR1_SPE;
        writel(val, spi_base + REG_CR1);
    }
}

static rt_ssize_t ls2k300_spi_io_polling_xfer(struct rt_spi_device *device, const void *send_buf,
                                              void *recv_buf, rt_size_t length)
{
    struct ls2k300_spi *spi = (struct ls2k300_spi *)device->bus;
    rt_uint8_t *base = spi_io_get_reg_base(spi->spi_num);
    rt_uint8_t *tx_buf = (rt_uint8_t *)send_buf;
    rt_uint8_t *rx_buf = (rt_uint8_t *)recv_buf;
    rt_size_t remain = length;
    rt_size_t chunk_size;
    rt_uint32_t val;
    int i, ret;

    RT_ASSERT(base != RT_NULL);

    while (remain > 0)
    {
        /* SPI-IO can transfer max 4 words at once */
        chunk_size = (remain > 4) ? 4 : remain;

        /* Set RX FIFO threshold */
        val = readl(base + REG_CR2);
        val &= ~(0x3 << 8);
        val |= (((chunk_size - 1) & 0x3) << 8);
        writel(val, base + REG_CR2);

        /* Set transfer frame count */
        val = (chunk_size - 1);
        writel(val, base + REG_CR3);

        /* Enable SPI */
        val = readl(base + REG_CR1);
        val |= BIT_CR1_SPE;
        writel(val, base + REG_CR1);

        /* Clear status */
        writel(0xffff, base + REG_SR1);

        /* Wait for TX available */
        ret = spi_io_wait_sr_event(base, REG_SR1, BIT_SR1_TXA, 10000);
        if (ret != RT_EOK)
        {
            LOG_E("SPI%d wait TXA timeout", spi->spi_num);
            return -RT_ETIMEOUT;
        }

        /* Write data to DR */
        for (i = 0; i < chunk_size; i++)
        {
            if (tx_buf)
            {
                writeb(tx_buf[i], base + REG_DR);
            }
            else
            {
                writeb(0xFF, base + REG_DR);
            }
        }

        /* Start transfer */
        val = readl(base + REG_CR1);
        val |= (BIT_CR1_CSTART | BIT_CR1_AUTOSUS);
        writel(val, base + REG_CR1);

        /* Wait for transfer complete */
        ret = spi_io_wait_sr_event(base, REG_SR1, BIT_SR1_EOT, 10000);
        if (ret != RT_EOK)
        {
            LOG_E("SPI%d wait EOT timeout", spi->spi_num);
            return -RT_ETIMEOUT;
        }
        writel(BIT_SR1_EOT, base + REG_SR1);

        /* Wait for RX available and read data */
        ret = spi_io_wait_sr_event(base, REG_SR1, BIT_SR1_RXA, 10000);
        if (ret != RT_EOK)
        {
            LOG_E("SPI%d wait RXA timeout", spi->spi_num);
            return -RT_ETIMEOUT;
        }

        for (i = 0; i < chunk_size; i++)
        {
            rt_uint8_t data = readb(base + REG_DR);
            if (rx_buf)
            {
                rx_buf[i] = data;
            }
        }

        if (tx_buf)
        {
            tx_buf += chunk_size;
        }
        if (rx_buf)
        {
            rx_buf += chunk_size;
        }
        remain -= chunk_size;
    }

    return length;
}

#ifdef SPI_IO_XFER_USING_DMA
static rt_ssize_t ls2k300_spi_io_dma_xfer(struct rt_spi_device *device, const void *send_buf,
                                          void *recv_buf, rt_size_t length)
{
    struct ls2k300_spi *spi = (struct ls2k300_spi *)device->bus;
    rt_uint8_t *base = spi_io_get_reg_base(spi->spi_num);
    rt_uint32_t val;
    rt_int32_t rx_dma_channel;

    RT_ASSERT(base != RT_NULL);

    /* Initialize DMA */
    dma_transfer_info_init(&spi->tx_dma);
    dma_transfer_info_init(&spi->rx_dma);

    rx_dma_channel = SPI_DMA_RX_CHANNEL(spi->spi_num);

    /* Configure TX DMA */
    spi->tx_dma.dma_channel = rx_dma_channel + 1;
    spi->tx_dma.m_inc = 1;
    spi->tx_dma.dir = DMA_DIR_READ_M;
    spi->tx_dma.size = length;
    spi->tx_dma.dma_priority = DMA_PRIORITY_3;
    spi->tx_dma.p_addr = (rt_uint64_t)(base + 0x40);
    spi->tx_dma.m_addr = (rt_uint64_t)send_buf;
    spi->tx_dma.dma_callback = RT_NULL;

    /* Configure RX DMA */
    spi->rx_dma.dma_channel = rx_dma_channel;
    spi->rx_dma.m_inc = 1;
    spi->rx_dma.size = length;
    spi->rx_dma.dma_priority = DMA_PRIORITY_3;
    spi->rx_dma.p_addr = (rt_uint64_t)(base + 0x40);
    spi->rx_dma.m_addr = (rt_uint64_t)recv_buf;
    spi->rx_dma.dma_callback = ls2k300_spi_io_dma_callback;
    spi->rx_dma.dma_callback_param = (void *)spi;

    /* Setup DMA mapping */
    dma_map_setup_type_4x2_channel(SPI_2_DMA_MAP + (spi->spi_num - MIN_SPI_NUM),
                                   dma_channel_01 + (spi->spi_num - MIN_SPI_NUM));

    /* Enable DMA in SPI controller */
    val = readl(base + REG_CR2);
    val |= (BIT_CR2_RXDMAEN | BIT_CR2_TXDMAEN);
    writel(val, base + REG_CR2);

    /* Clear status */
    writel(0xffff, base + REG_SR1);

    /* Wait for TX available */
    if (spi_io_wait_sr_event(base, REG_SR1, BIT_SR1_TXA, 10000) != RT_EOK)
    {
        LOG_E("SPI%d DMA wait TXA timeout", spi->spi_num);
        return -RT_ETIMEOUT;
    }

    /* Configure DMA channels */
    dma_config(&spi->tx_dma);
    dma_config(&spi->rx_dma);

    /* Prepare completion */
    rt_completion_init(&spi->transfer_done);

    /* Disable auto-suspend for continuous transfer */
    val = readl(base + REG_CR1);
    val |= BIT_CR1_CSTART;
    val &= ~BIT_CR1_AUTOSUS;
    writel(val, base + REG_CR1);

    /* Enable SPI */
    val |= BIT_CR1_SPE;
    writel(val, base + REG_CR1);

    /* Start DMA */
    dma_start(&spi->tx_dma);
    dma_start(&spi->rx_dma);

    /* Wait for completion */
    rt_completion_wait(&spi->transfer_done, RT_WAITING_FOREVER);

    /* Cleanup */
    dma_stop(&spi->tx_dma);
    dma_stop(&spi->rx_dma);

    return length;
}
#endif /* SPI_IO_XFER_USING_DMA */

static rt_ssize_t ls2k300_spi_io_xfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    struct ls2k300_spi *spi = (struct ls2k300_spi *)device->bus;
    rt_err_t result = RT_EOK;
    rt_size_t len = 0;

    RT_ASSERT(device != RT_NULL);
    RT_ASSERT(message != RT_NULL);

    /* Take SPI bus lock */

    /* CS control: assert */
    if (message->cs_take)
    {
        ls2k300_spi_io_cs_control(device, RT_TRUE);
    }

    /* Configure SPI if needed */
    if (device->bus->owner != device)
    {
        result = ls2k300_spi_io_configure(device, &device->config);
        if (result != RT_EOK)
        {
            goto _exit;
        }
        device->bus->owner = device;
    }

    /* Data transfer */
    if (message->length > 0)
    {
#ifdef SPI_IO_XFER_USING_DMA
        /* Use DMA for large transfers */
        if (spi->dma_support && message->length > 32)
        {
            len = ls2k300_spi_io_dma_xfer(device, message->send_buf, message->recv_buf,
                                          message->length);
        }
        else
#endif
        {
            len = ls2k300_spi_io_polling_xfer(device, message->send_buf, message->recv_buf,
                                              message->length);
        }
    }

_exit:
    /* CS control: release */
    if (message->cs_release)
    {
        ls2k300_spi_io_cs_control(device, RT_FALSE);
    }

    return (result == RT_EOK) ? len : (rt_ssize_t)result;
}

static const struct rt_spi_ops ls2k300_spi_ops = {
    .configure = ls2k300_spi_io_configure,
    .xfer = ls2k300_spi_io_xfer,
};

static int ls2k300_spi_io_hw_init(rt_uint8_t spi_num)
{
    rt_uint8_t *spi_io_base;
    rt_uint32_t val = 0;

    spi_io_base = spi_io_get_reg_base(spi_num);
    if (spi_io_base == RT_NULL)
    {
        return -RT_ERROR;
    }

    /* Set controller mode and full duplex */
    val = (BIT_CFG3_MSTR | BIT_CFG3_DIE | BIT_CFG3_DOE);
    writel(val, spi_io_base + REG_CFG3);

    /* Enable SPI and set CS high (inactive) */
    val = readl(spi_io_base + REG_CR1);
    val |= (BIT_CR1_SPE | BIT_CR1_SSREV);
    writel(val, spi_io_base + REG_CR1);

    return RT_EOK;
}

int rt_hw_spi_io_init(void)
{
    int result;
    rt_uint8_t i;

    for (i = 0; i < 2; i++)
    {
        rt_memset(&spi_bus[i], 0, sizeof(struct ls2k300_spi));

        spi_bus[i].spi_num = MIN_SPI_NUM + i;
        spi_bus[i].ref_clk = SPI_IO_REF_CLK;
        spi_bus[i].max_speed_hz = (SPI_IO_REF_CLK) >> 3;
        spi_bus[i].current_speed = 0;
        spi_bus[i].dma_support = 1;

        /* Initialize hardware */
        result = ls2k300_spi_io_hw_init(spi_bus[i].spi_num);
        if (result != RT_EOK)
        {
            LOG_E("SPI%d hardware init failed", spi_bus[i].spi_num);
            continue;
        }

        /* Register SPI bus */
        result = rt_spi_bus_register(&spi_bus[i].parent,
                                     (spi_bus[i].spi_num == 2) ? "spi2" : "spi3", &ls2k300_spi_ops);
        if (result != RT_EOK)
        {
            LOG_E("SPI%d bus register failed", spi_bus[i].spi_num);
        }
        else
        {
            LOG_I("SPI%d bus registered successfully", spi_bus[i].spi_num);
        }
    }

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_spi_io_init);

/**
 * @brief Attach SPI device to bus with CS pin configuration (RT-Thread v5.2.0 standard)
 *
 * This is the recommended interface for attaching SPI devices in RT-Thread v5.2.0.
 *
 * @param bus_name      SPI bus name, e.g., "spi2", "spi3"
 * @param device_name   SPI device name, e.g., "spi20", "spi30"
 * @param cs_pin        CS pin number, use -1 for hardware CS
 * @param user_data     User data, can be used for device-specific configuration
 *
 * @return rt_err_t     RT_EOK on success, negative error code on failure
 */
rt_err_t rt_hw_spi_io_device_attach(const char *bus_name, const char *device_name, rt_base_t cs_pin,
                                    void *user_data)
{
    rt_err_t result;
    struct rt_spi_device *spi_device;
    struct ls2k300_spi_cs *cs_pin_config;

    /* Allocate memory for SPI device and CS configuration */
    spi_device = (struct rt_spi_device *)rt_malloc(sizeof(struct rt_spi_device));
    if (spi_device == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    cs_pin_config = (struct ls2k300_spi_cs *)rt_malloc(sizeof(struct ls2k300_spi_cs));
    if (cs_pin_config == RT_NULL)
    {
        rt_free(spi_device);
        return -RT_ENOMEM;
    }

    /* Configure CS pin */
    cs_pin_config->pin = cs_pin;
    cs_pin_config->active_low = RT_TRUE;  /* Default: active low */

    /* If using GPIO CS, configure pin mode */
    if (cs_pin >= 0)
    {
        rt_pin_mode(cs_pin, PIN_MODE_OUTPUT);
        rt_pin_write(cs_pin, PIN_HIGH);  /* Deselect device */
    }

    result = rt_spi_bus_attach_device_cspin(spi_device, device_name, bus_name, cs_pin,
                                            (void *)cs_pin_config);

    if (result != RT_EOK)
    {
        LOG_E("Failed to attach %s to %s", device_name, bus_name);
        rt_free(spi_device);
        rt_free(cs_pin_config);
        return result;
    }

    LOG_I("SPI device %s attached to %s (CS pin: %d)", device_name, bus_name, cs_pin);
    return RT_EOK;
}
