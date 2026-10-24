/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     OpenLANPU Contributors    connect SDIO0 to RT-Thread MMC/SD
 *
 */

/*
 * Description:
 *   Registers the board SD memory host, interrupt handling and DMA storage.
 *
 * Notes:
 *   Firmware supplies card power. No card-detect GPIO is assumed: insert the
 *   card before boot and unmount before removing it. Only 3.3 V SDR is exposed.
 *   MMC/SD bus_lock serializes requests; IRQ code only records status/wakes.
 *
 */

#include <rtthread.h>
#include <rthw.h>
#include <drivers/dev_mmcsd_core.h>
#include "ls2k.h"
#include "ls2k0300.h"
#include "ls2k_sdio.h"

#ifdef BSP_USING_SDIO

#define DBG_TAG "ls2k.sdio"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

struct ls2k_sd_host
{
    struct ls2k_sdio controller;
    struct rt_mmcsd_host *host;
    struct rt_semaphore event;
    struct rt_spinlock lock;
    rt_ubase_t base;
    rt_uint32_t pending;
    rt_uint32_t requests, errors, interrupts;
    int irq;
};

static struct ls2k_sd_host device;
static struct ls2k_sdio_dma storage __attribute__((section(".sdio_dma"), aligned(64)));

static uint32_t sd_read(void *context, unsigned int reg)
{
    struct ls2k_sd_host *dev = context;
    uint32_t value;

    if (reg == LSD_STATUS)
    {
        rt_base_t level = rt_spin_lock_irqsave(&dev->lock);

        value = HWREG32(dev->base + reg);
        HWREG32(dev->base + reg) = value;
        value |= dev->pending;
        dev->pending = 0;
        rt_hw_dmb();
        rt_spin_unlock_irqrestore(&dev->lock, level);
    }
    else
    {
        value = HWREG32(dev->base + reg);
    }

    return value;
}

static void sd_write(void *context, unsigned int reg, uint32_t value)
{
    struct ls2k_sd_host *dev = context;

    if (reg == LSD_STATUS)
    {
        /* sd_read consumes status atomically with the ISR. Only an explicit
         * all-status clear starts/ends a request; a second W1C could lose an
         * event that arrived between read and write. */
        if (value == LSD_ALL_STATUS)
        {
            rt_base_t level = rt_spin_lock_irqsave(&dev->lock);

            dev->pending = 0;
            HWREG32(dev->base + reg) = value;
            rt_hw_dmb();
            rt_spin_unlock_irqrestore(&dev->lock, level);
        }
    }
    else
    {
        HWREG32(dev->base + reg) = value;
        rt_hw_dmb();
    }
}

static uint32_t sd_time(void *context)
{
    RT_UNUSED(context);
    return (uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
}

static void sd_wait(void *context, unsigned int ms)
{
    struct ls2k_sd_host *dev = context;

    /* A bounded poll also handles a lost IRQ; stale semaphore tokens never
     * complete a command because the engine always checks hardware status. */
    rt_sem_take(&dev->event, rt_tick_from_millisecond(ms));
}

static void sd_barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static const struct ls2k_sdio_io sd_io =
{
    sd_read, sd_write, sd_time, sd_wait, sd_barrier
};

static void sd_interrupt(int vector, void *parameter)
{
    struct ls2k_sd_host *dev = parameter;
    rt_base_t level = rt_spin_lock_irqsave(&dev->lock);
    uint32_t status = HWREG32(dev->base + LSD_STATUS) & LSD_REQUEST_IRQS;

    RT_UNUSED(vector);
    HWREG32(dev->base + LSD_STATUS) = status;
    dev->pending |= status;
    dev->interrupts++;
    rt_hw_dmb();
    rt_spin_unlock_irqrestore(&dev->lock, level);

    if (status)
    {
        rt_sem_release(&dev->event);
    }
}

static int sd_error(int error)
{
    switch (error)
    {
    case LSD_OK:
        return RT_EOK;
    case LSD_TIMEOUT:
        return -RT_ETIMEOUT;
    case LSD_INVALID:
        return -RT_EINVAL;
    default:
        return -RT_EIO;
    }
}

static void sd_command(struct ls2k_sd_host *dev, struct rt_mmcsd_cmd *cmd)
{
    struct ls2k_sdio_command transfer = {0};
    struct rt_mmcsd_data *data = cmd->data;
    unsigned int response = resp_type(cmd);

    /* This host exposes SD memory only, not eMMC or SDIO function devices. */
    if (cmd->cmd_code == SEND_OP_COND || cmd->cmd_code == SD_IO_SEND_OP_COND)
    {
        cmd->err = -RT_ENOSYS;
        return;
    }

    transfer.index = cmd->cmd_code;
    transfer.argument = cmd->arg;
    transfer.response_present = response != RESP_NONE;
    transfer.response_long = response == RESP_R2;
    transfer.response_busy = response == RESP_R1B;
    transfer.response_crc = response != RESP_NONE && response != RESP_R3 && response != RESP_R4;
    transfer.timeout_ms = data || transfer.response_busy ? 5000 : 1000;

    if (cmd->busy_timeout && cmd->busy_timeout <= 10000)
    {
        transfer.timeout_ms = cmd->busy_timeout;
    }

    if (data)
    {
        data->bytes_xfered = 0;
        data->err = 0;
        if (!data->buf || data->sg_len || (data->flags & DATA_STREAM) ||
            ((data->flags & (DATA_DIR_READ | DATA_DIR_WRITE)) != DATA_DIR_READ &&
             (data->flags & (DATA_DIR_READ | DATA_DIR_WRITE)) != DATA_DIR_WRITE))
        {
            cmd->err = data->err = -RT_EINVAL;
            return;
        }

        transfer.buffer = data->buf;
        transfer.blocks = data->blks;
        transfer.block_size = data->blksize;
        transfer.write = !!(data->flags & DATA_DIR_WRITE);
    }

    dev->requests++;
    cmd->err = sd_error(ls2k_sdio_transfer(&dev->controller, &transfer));
    rt_memcpy(cmd->resp, transfer.response, sizeof(cmd->resp));
    if (cmd->err)
    {
        dev->errors++;
    }

    if (data)
    {
        data->err = cmd->err;
        if (!cmd->err)
        {
            data->bytes_xfered = data->blks * data->blksize;
        }
    }
}

static void sd_request(struct rt_mmcsd_host *host, struct rt_mmcsd_req *request)
{
    struct ls2k_sd_host *dev = host->private_data;

    if (request->sbc)
    {
        sd_command(dev, request->sbc);
        if (request->sbc->err)
        {
            request->cmd->err = request->sbc->err;
            if (request->data)
            {
                request->data->err = request->sbc->err;
                request->data->bytes_xfered = 0;
            }

            mmcsd_req_complete(host);
            return;
        }
    }

    sd_command(dev, request->cmd);
    if (request->stop && !dev->controller.fault)
    {
        sd_command(dev, request->stop);
        if (request->stop->err && request->data)
        {
            request->data->err = request->stop->err;
            request->data->bytes_xfered = 0;
        }
    }

    mmcsd_req_complete(host);
}

static void sd_iocfg(struct rt_mmcsd_host *host, struct rt_mmcsd_io_cfg *config)
{
    struct ls2k_sd_host *dev = host->private_data;
    struct ls2k_sdio *sd = &dev->controller;

    if (sd->fault)
    {
        return;
    }

    sd->width = config->bus_width == MMCSD_BUS_WIDTH_4 ? 4 : 1;
    if (config->power_mode == MMCSD_POWER_OFF)
    {
        sd_write(dev, LSD_IRQ_ENABLE, 0);
        ls2k_sdio_clock(sd, 0);
        return;
    }

    if (config->power_mode == MMCSD_POWER_UP && ls2k_sdio_reset(sd))
    {
        LOG_E("controller reset failed");
        return;
    }

    if (config->bus_width != MMCSD_BUS_WIDTH_1 && config->bus_width != MMCSD_BUS_WIDTH_4)
    {
        sd->fault = 1;
        LOG_E("unsupported bus width");
        return;
    }

    if (ls2k_sdio_clock(sd, config->clock))
    {
        sd->fault = 1;
        LOG_E("unsupported clock %u Hz", config->clock);
    }
}

static rt_int32_t sd_card_present(struct rt_mmcsd_host *host)
{
    RT_UNUSED(host);
    /* No CD GPIO is described by the chip DTS; requests detect an absent card. */
    return 1;
}

static const struct rt_mmcsd_host_ops sd_ops =
{
    .request = sd_request,
    .set_iocfg = sd_iocfg,
    .get_card_status = sd_card_present,
};

static int sd_platform_init(struct ls2k_sd_host *dev)
{
    const unsigned int port = BSP_SDIO_PORT;
    uint32_t sample = HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SAMPLE));
    uint32_t scale = HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SCALE));
    uint32_t root, divider = (scale >> 24) & 15, old_divider;
    rt_ubase_t mux;
    uint32_t mask;

    if (!port && (sample & (1U << 6)))
    {
        LOG_E("SDIO0 is strapped for 1.8 V; a 3.3 V SD card is required");
        return -RT_EINVAL;
    }

    root = BSP_SDIO_ROOT_CLOCK_HZ;
    if (!root)
    {
        /* DEVICE PLL before peripheral scaling; reuse the validated decoder. */
        root = ls2k0300_apb_clock(sample,
                HWREG32(LS2K_UNCACHED(LS2K0300_DDR_PLL0)),
                HWREG32(LS2K_UNCACHED(LS2K0300_DDR_PLL1)), 1U << 19);
    }

    if (!root)
    {
        return -RT_EINVAL;
    }

    if (!divider)
    {
        divider = 1;
    }

    old_divider = divider;

    /* Both ports share this divider. Avoid the vendor driver's >400 kHz
     * identification clock, but do not disturb an active second controller. */
    while (root / divider > 400000U * 255U && divider < 15)
    {
        divider++;
    }

    if (root / divider > 400000U * 255U)
    {
        return -RT_EINVAL;
    }

    if (divider != old_divider)
    {
        rt_ubase_t other = LS2K_UNCACHED(0x16140000UL + (1U - port) * 0x8000UL);

        if (HWREG32(other + LSD_CON) & LSD_CLOCK)
        {
            LOG_E("shared SDIO clock needs division; stop the other port first");
            return -RT_EBUSY;
        }
    }

    HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SCALE)) =
        (scale & ~(15U << 24)) | (divider << 24);
    HWREG32(LS2K_UNCACHED(0x16000424UL)) |= 1U << 4;
    HWREG32(LS2K_UNCACHED(0x16000118UL)) |= 1U << (13 + port);
    HWREG32(LS2K_UNCACHED(0x16000120UL)) |= 1U << (13 + port);

    /* Only CLK/CMD/D0..D3; SDIO0 D4..D7 may still carry unrelated signals. */
    mux = LS2K_UNCACHED(port ? 0x160004a8UL : 0x160004a4UL);
    mask = port ? 0x000fff00U : 0xfff00000U;
    HWREG32(mux) = (HWREG32(mux) & ~mask) | mask;
    rt_hw_dmb();

    dev->base = LS2K_UNCACHED(0x16140000UL + port * 0x8000UL);
    dev->irq = port ? 32 : 31;
    dev->controller.base_address = LS2K_PHYSICAL(dev->base);
    dev->controller.input_clock = root / divider;
    /* BUS_SEL swaps the strap-selected SD/eMMC controller; DATA_MODE=0 SDR. */
    HWREG32(dev->base + LSD_BUS_MODE) =
        (HWREG32(dev->base + LSD_BUS_MODE) & ~3U) | (((sample >> (3 + port)) & 1U) << 1);
    rt_hw_dmb();
    return RT_EOK;
}

static int rt_hw_sdio_init(void)
{
    struct ls2k_sd_host *dev = &device;
    struct rt_mmcsd_host *host;
    int result;

    if (LS2K_PHYSICAL(&storage) + sizeof(storage) > LS2K0300_DMA_LIMIT)
    {
        return -RT_EINVAL;
    }

    result = sd_platform_init(dev);
    if (result)
    {
        LOG_E("platform initialization failed (%d)", result);
        return result;
    }

    rt_spin_lock_init(&dev->lock);
    rt_sem_init(&dev->event, "sd_irq", 0, RT_IPC_FLAG_PRIO);
    dev->controller.io = &sd_io;
    dev->controller.context = dev;
    dev->controller.dma = (void *)LS2K_UNCACHED(&storage);
    dev->controller.dma_address = LS2K_PHYSICAL(&storage);
    dev->controller.width = 1;

    /* Reset the controller before touching storage that firmware might use. */
    if (ls2k_sdio_reset(&dev->controller))
    {
        rt_sem_detach(&dev->event);
        return -RT_EIO;
    }

    rt_memset(dev->controller.dma, 0, sizeof(storage));
    host = mmcsd_alloc_host();
    if (!host)
    {
        rt_sem_detach(&dev->event);
        return -RT_ENOMEM;
    }

    dev->host = host;
    host->private_data = dev;
    host->ops = &sd_ops;
    host->freq_min = (dev->controller.input_clock + 254) / 255;
    host->freq_max = BSP_SDIO_MAX_CLOCK_HZ < dev->controller.input_clock ?
                     BSP_SDIO_MAX_CLOCK_HZ : dev->controller.input_clock;
    host->valid_ocr = VDD_32_33 | VDD_33_34;
    host->flags = MMCSD_BUSWIDTH_4 | MMCSD_MUTBLKWRITE;
    host->max_seg_size = LSD_BUFFER_SIZE;
    host->max_dma_segs = 1;
    host->max_blk_size = 512;
    host->max_blk_count = LSD_BUFFER_SIZE / 512;

    rt_hw_interrupt_mask(dev->irq);
    rt_hw_interrupt_install(dev->irq, sd_interrupt, dev, "sdio");
    rt_hw_interrupt_umask(dev->irq);
    LOG_I("SDIO%u at 0x%x irq=%d input=%u Hz, 3.3 V SDR <=%u Hz",
          BSP_SDIO_PORT, dev->controller.base_address, dev->irq,
          dev->controller.input_clock, host->freq_max);
    mmcsd_change(host);
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_sdio_init);

#ifdef RT_USING_FINSH
static void sdio_status(void)
{
    struct ls2k_sd_host *dev = &device;
    rt_uint32_t interrupts;
    rt_base_t level;

    if (!dev->host)
    {
        rt_kprintf("SDIO host not initialized\n");
        return;
    }

    mmcsd_host_lock(dev->host);
    level = rt_spin_lock_irqsave(&dev->lock);
    interrupts = dev->interrupts;
    rt_spin_unlock_irqrestore(&dev->lock, level);
    rt_kprintf("SDIO%u card=%s clock=%u width=%u requests=%u errors=%u irq=%u fault=%d\n",
               BSP_SDIO_PORT, dev->host->card ? "present" : "absent",
               dev->controller.clock, dev->controller.width, dev->requests,
               dev->errors, interrupts, dev->controller.fault);
    mmcsd_host_unlock(dev->host);
}
MSH_CMD_EXPORT(sdio_status, Show SDIO controller and request status);
#endif

#endif /* BSP_USING_SDIO */
