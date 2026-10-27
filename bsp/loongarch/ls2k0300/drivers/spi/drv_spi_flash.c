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
 *   LS2K0300 development BSP: drv spi flash.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include "drv_spi_flash.h"
#include "loongson_soc_info.h"
#include "loongson_board_info.h"

#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "drv.spi_flash"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define MAX_SPI_NUM 1   /* SPI0 and SPI1, but SPI0 reserved for flash */
#define SPI_REF_CLK ls2k_dev_apb_clock()

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* SPI bus device structure for Flash controller */
struct ls2k300_spi_flash
{
    struct rt_spi_bus parent;
    rt_uint8_t spi_num;
    rt_uint32_t ref_clk;
    rt_uint32_t current_speed;
};

/* SPI CS pin control structure */
struct ls2k300_spi_flash_cs
{
    rt_base_t pin;        /* CS pin number, -1 for hardware CS */
    rt_bool_t active_low; /* CS active level */
    rt_uint8_t cs_index;   /* Hardware CS index (0-3) */
};

static struct ls2k300_spi_flash spi_flash_bus[2];   /* SPI0 and SPI1 */

/* SPI clock divider options: {spre, spr} encoded value */
/* Divisors: 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 */
static const uint8_t spi_clk_options[12] = { 0, 1, 4, 2, 3, 5, 6, 7, 8, 9, 10, 11 };

static inline rt_uint8_t *spi_flash_get_reg_base(rt_uint8_t spi_num)
{
    rt_uint8_t *res;

    if (spi_num > MAX_SPI_NUM)
    {
        return RT_NULL;
    }

    res = (rt_uint8_t *)LS_SPI0_BASE;
    res += (spi_num * 0x8000);
    return res;
}

static inline rt_uint8_t spi_flash_num_invalid_check(int spi_num)
{
    /* SPI0 is reserved for onboard flash, recommend using SPI1 for external devices */
    if (spi_num > MAX_SPI_NUM)
    {
        return 1;
    }
    return 0;
}

/* Choose the slowest necessary divider; never exceed the requested clock. */
static unsigned int spi_flash_get_div_index(unsigned int max_speed_hz)
{
    unsigned long needed = DIV_ROUND_UP(SPI_REF_CLK, max_speed_hz);
    unsigned int index = 0;

    while (index < 11 && (2UL << index) < needed)
    {
        ++index;
    }

    return index;
}

static rt_err_t spi_flash_wait_txrx_done(rt_uint8_t spi_num)
{
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_num);
    int timeout = LS_SPI_TX_TIMEOUT;

    while (timeout--)
    {
        if (LS_SPI_SPSR_SPIF_MASK & readb(spi_base + LS_SPI_SPSR_OFFSET))
        {
            return RT_EOK;
        }
        rt_hw_us_delay(1);
    }

    LOG_E("SPI%d transfer timeout", spi_num);
    return -RT_ETIMEOUT;
}

static void spi_flash_clear_events(rt_uint8_t spi_num)
{
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_num);
    rt_uint8_t val;

    /* Clear transfer complete flag */
    val = readb(spi_base + LS_SPI_SPSR_OFFSET);
    val |= LS_SPI_SPSR_SPIF_MASK;
    writeb(val, spi_base + LS_SPI_SPSR_OFFSET);

    /* Clear write collision flag if set */
    val = readb(spi_base + LS_SPI_SPSR_OFFSET);
    if (LS_SPI_SPSR_WCOL_MASK & val)
    {
        writeb(val & ~LS_SPI_SPSR_WCOL_MASK, spi_base + LS_SPI_SPSR_OFFSET);
    }
}

static void spi_flash_enable(rt_uint8_t spi_num, rt_bool_t enable)
{
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_num);
    rt_uint8_t val;

    val = readb(spi_base + LS_SPI_SPCR_OFFSET);
    if (enable)
    {
        val |= 0x50; /* SPE | master mode */
    }
    else
    {
        val &= ~0x50;
    }
    writeb(val, spi_base + LS_SPI_SPCR_OFFSET);
}

static rt_err_t ls2k300_spi_flash_configure(struct rt_spi_device *device,
                                            struct rt_spi_configuration *configuration)
{
    struct ls2k300_spi_flash *spi_flash = (struct ls2k300_spi_flash *)device->bus;
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_flash->spi_num);
    rt_uint8_t val;
    unsigned int div_index;
    rt_uint8_t spre, spr;

    RT_ASSERT(spi_base != RT_NULL);

    if (!configuration->max_hz || configuration->data_width != 8 ||
        (configuration->mode & (RT_SPI_SLAVE | RT_SPI_LSB)) || !SPI_REF_CLK ||
        configuration->max_hz < DIV_ROUND_UP(SPI_REF_CLK, 4096))
    {
        return -RT_EINVAL;
    }

    /* Calculate clock divider */
    div_index = spi_flash_get_div_index(configuration->max_hz);
    if (div_index >= 12)
    {
        div_index = 11;
    }

    spre = (spi_clk_options[div_index] >> 2) & 0x3;
    spr = spi_clk_options[div_index] & 0x3;

    /* Disable SPI first */
    spi_flash_enable(spi_flash->spi_num, RT_FALSE);

    /* Set SPR (clock rate) */
    val = readb(spi_base + LS_SPI_SPCR_OFFSET);
    val &= ~LS_SPI_SPCR_SPR_MASK;
    val |= (spr & 0x3);
    /* Set CPOL and CPHA */
    val &= ~(LS_SPI_SPCR_CPOL_MASK | LS_SPI_SPCR_CPHA_MASK);
    if (configuration->mode & RT_SPI_CPOL)
    {
        val |= LS_SPI_SPCR_CPOL_MASK;
    }
    if (configuration->mode & RT_SPI_CPHA)
    {
        val |= LS_SPI_SPCR_CPHA_MASK;
    }
    /* Enable SPI in master mode, disable interrupt */
    val |= 0x10; /* Master mode */
    writeb(val, spi_base + LS_SPI_SPCR_OFFSET);

    /* Set SPRE (extended clock rate) */
    val = readb(spi_base + LS_SPI_SPER_OFFSET);
    val &= ~LS_SPI_SPER_SPRE_MASK;
    val |= (spre & 0x3);
    writeb(val, spi_base + LS_SPI_SPER_OFFSET);

    /* Clear status */
    writeb(0xc0, spi_base + LS_SPI_SPSR_OFFSET);

    /* Set parameter: software CS control */
    val = readb(spi_base + LS_SPI_SFC_PARAM_OFFSET);
    val &= 0xfe;
    writeb(val, spi_base + LS_SPI_SFC_PARAM_OFFSET);

    /* Set timing */
    writeb(0x05, spi_base + LS_SPI_SFC_TIMING_OFFSET);

    spi_flash->current_speed = configuration->max_hz;

    LOG_D("SPI Flash%d configured: speed=%d, mode=0x%x", spi_flash->spi_num, configuration->max_hz,
          configuration->mode);

    return RT_EOK;
}

static void ls2k300_spi_flash_cs_control(struct rt_spi_device *device, rt_bool_t enable)
{
    struct ls2k300_spi_flash_cs *cs = (struct ls2k300_spi_flash_cs *)device->parent.user_data;
    struct ls2k300_spi_flash *spi_flash = (struct ls2k300_spi_flash *)device->bus;
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_flash->spi_num);
    rt_uint8_t val;

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
        /* Use hardware CS via SFC_SOFTCS register */
        val = readb(spi_base + LS_SPI_SFC_SOFTCS_OFFSET);
        val |= (0x11 << cs->cs_index); /* Enable CS */
        if (enable)
        {
            /* Active (low) */
            val ^= (0x10 << cs->cs_index);
        }
        writeb(val, spi_base + LS_SPI_SFC_SOFTCS_OFFSET);

        val = readb(spi_base + LS_SPI_SFC_PARAM_OFFSET);
        val = readb(spi_base + LS_SPI_SFC_SOFTCS_OFFSET);
    }
}

static rt_ssize_t ls2k300_spi_flash_xfer(struct rt_spi_device *device,
                                         struct rt_spi_message *message)
{
    struct ls2k300_spi_flash *spi_flash = (struct ls2k300_spi_flash *)device->bus;
    rt_uint8_t *spi_base = spi_flash_get_reg_base(spi_flash->spi_num);
    rt_uint8_t *tx_buf = (rt_uint8_t *)message->send_buf;
    rt_uint8_t *rx_buf = (rt_uint8_t *)message->recv_buf;
    rt_size_t count = message->length;
    rt_size_t i;
    rt_err_t result = RT_EOK;
    rt_uint8_t rx_data;

    RT_ASSERT(spi_base != RT_NULL);

    /* The RT-Thread SPI core holds the bus mutex across this transfer. */

    /* CS assert */
    if (message->cs_take)
    {
        ls2k300_spi_flash_cs_control(device, RT_TRUE);
    }

    /* Configure if needed */
    if (device->bus->owner != device)
    {
        result = ls2k300_spi_flash_configure(device, &device->config);
        if (result != RT_EOK)
        {
            goto _exit;
        }
        device->bus->owner = device;
    }

    /* Enable SPI */
    spi_flash_enable(spi_flash->spi_num, RT_TRUE);

    /* Transfer data byte by byte */
    for (i = 0; i < count; i++)
    {
        /* Write data */
        if (tx_buf)
        {
            writeb(tx_buf[i], spi_base + LS_SPI_TxFIFO_OFFSET);
        }
        else
        {
            writeb(0xFF, spi_base + LS_SPI_TxFIFO_OFFSET); /* Dummy write */
        }

        /* Wait for transfer complete */
        result = spi_flash_wait_txrx_done(spi_flash->spi_num);
        if (result != RT_EOK)
        {
            LOG_E("SPI Flash%d transfer failed at byte %d", spi_flash->spi_num, i);
            goto _exit;
        }

        /* Read data */
        rx_data = readb(spi_base + LS_SPI_RxFIFO_OFFSET);
        if (rx_buf)
        {
            rx_buf[i] = rx_data;
        }

        /* Clear events */
        spi_flash_clear_events(spi_flash->spi_num);
    }

_exit:
    /* Disable SPI */
    spi_flash_enable(spi_flash->spi_num, RT_FALSE);

    /* CS release */
    if (message->cs_release)
    {
        ls2k300_spi_flash_cs_control(device, RT_FALSE);
    }

    return (result == RT_EOK) ? (rt_ssize_t)count : (rt_ssize_t)result;
}

static const struct rt_spi_ops ls2k300_spi_flash_ops = {
    .configure = ls2k300_spi_flash_configure,
    .xfer = ls2k300_spi_flash_xfer,
};

static int ls2k300_spi_flash_hw_init(rt_uint8_t spi_num)
{
    rt_uint8_t *spi_base;
    rt_uint8_t val;

    if (spi_flash_num_invalid_check(spi_num))
    {
        return -RT_ERROR;
    }

    spi_base = spi_flash_get_reg_base(spi_num);
    if (spi_base == RT_NULL)
    {
        return -RT_ERROR;
    }

    /* Initialize controller: master mode, disable interrupt */
    writeb(0x13, spi_base + LS_SPI_SPCR_OFFSET);

    /* Clear status */
    writeb(0xc0, spi_base + LS_SPI_SPSR_OFFSET);

    /* Set external register: 1 byte interrupt, simultaneous read/write timing */
    writeb(0x03, spi_base + LS_SPI_SPER_OFFSET);

    /* Software CS control */
    val = readb(spi_base + LS_SPI_SFC_PARAM_OFFSET);
    val &= 0xfe;
    writeb(val, spi_base + LS_SPI_SFC_PARAM_OFFSET);

    /* Timing control */
    writeb(0x05, spi_base + LS_SPI_SFC_TIMING_OFFSET);

    /* Deselect all CS */
    writeb(0xff, spi_base + LS_SPI_SFC_SOFTCS_OFFSET);

    return RT_EOK;
}

int rt_hw_spi_flash_init(void)
{
    int result;
    rt_uint8_t i;

    for (i = 0; i <= MAX_SPI_NUM; i++)
    {
        /* Skip SPI0 if it's reserved for onboard flash boot */
        /* Uncomment below to skip SPI0 initialization */
        /*
        if (i == 0)
        {
            continue;
        }
        */

        rt_memset(&spi_flash_bus[i], 0, sizeof(struct ls2k300_spi_flash));

        spi_flash_bus[i].spi_num = i;
        spi_flash_bus[i].ref_clk = SPI_REF_CLK;
        spi_flash_bus[i].current_speed = 0;

        /* Initialize hardware */
        result = ls2k300_spi_flash_hw_init(i);
        if (result != RT_EOK)
        {
            LOG_E("SPI Flash%d hardware init failed", i);
            continue;
        }

        /* Register SPI bus */
        const char *bus_name = (i == 0) ? "spi0" : "spi1";
        result = rt_spi_bus_register(&spi_flash_bus[i].parent, bus_name, &ls2k300_spi_flash_ops);
        if (result != RT_EOK)
        {
            LOG_E("SPI Flash%d bus register failed", i);
        }
        else
        {
            LOG_I("SPI Flash%d bus registered successfully", i);
        }
    }

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_spi_flash_init);

/**
 * @brief Attach SPI Flash device to bus with CS pin configuration
 *
 * @param bus_name      SPI bus name, "spi0" or "spi1"
 * @param device_name   SPI device name
 * @param cs_pin        CS pin number, use -1 for hardware CS
 * @param user_data     User data (can store cs_index for hardware CS)
 *
 * @return rt_err_t     RT_EOK on success
 */
rt_err_t rt_hw_spi_flash_device_attach(const char *bus_name, const char *device_name,
                                       rt_base_t cs_pin, void *user_data)
{
    rt_err_t result;
    struct rt_spi_device *spi_device;
    struct ls2k300_spi_flash_cs *cs_config;

    spi_device = (struct rt_spi_device *)rt_malloc(sizeof(struct rt_spi_device));
    if (spi_device == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    cs_config = (struct ls2k300_spi_flash_cs *)rt_malloc(sizeof(struct ls2k300_spi_flash_cs));
    if (cs_config == RT_NULL)
    {
        rt_free(spi_device);
        return -RT_ENOMEM;
    }

    /* Configure CS */
    cs_config->pin = cs_pin;
    cs_config->active_low = RT_TRUE;
    cs_config->cs_index = 0; /* Default CS0 */

    /* Extract cs_index from user_data if provided */
    if (user_data != RT_NULL)
    {
        cs_config->cs_index = *(rt_uint8_t *)user_data & 0x3;
    }

    /* Setup GPIO if used */
    if (cs_pin >= 0)
    {
        rt_pin_mode(cs_pin, PIN_MODE_OUTPUT);
        rt_pin_write(cs_pin, PIN_HIGH);
    }

    result = rt_spi_bus_attach_device_cspin(spi_device, device_name, bus_name, cs_pin,
                                            (void *)cs_config);
    if (result != RT_EOK)
    {
        LOG_E("Failed to attach %s to %s", device_name, bus_name);
        rt_free(spi_device);
        rt_free(cs_config);
        return result;
    }

    LOG_I("SPI Flash device %s attached to %s (CS pin: %d)", device_name, bus_name, cs_pin);
    return RT_EOK;
}
