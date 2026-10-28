/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     LoongsonLab               connect SDIO0 to RT-Thread MMC/SD
 *
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   Registers independent SD/eMMC hosts, interrupt handling and DMA storage.
 *
 * Notes:
 *   Firmware supplies card power. No card-detect GPIO is assumed: insert the
 *   card before boot. SD requires 3.3 V, eMMC keeps its fixed board voltage.
 *   Only SDR is exposed; payload writes require explicit configuration.
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
    unsigned int port;
    unsigned int max_width;
};

#ifdef BSP_SDIO_DUAL_HOST
#define SD_HOST_COUNT 2
#else
#define SD_HOST_COUNT 1
#endif

static struct ls2k_sd_host devices[SD_HOST_COUNT];
static struct ls2k_sdio_dma storage[SD_HOST_COUNT] __attribute__((section(".sdio_dma"), aligned(64)));

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

static const struct ls2k_sdio_io sd_io = { sd_read, sd_write, sd_time, sd_wait, sd_barrier };

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

static void command_fail(struct rt_mmcsd_cmd *cmd, int error)
{
    cmd->err = error;
    rt_memset(cmd->resp, 0, sizeof(cmd->resp));

    if (cmd->data)
    {
        cmd->data->err = error;
        cmd->data->bytes_xfered = 0;
    }
}

static void sd_command(struct ls2k_sd_host *dev, struct rt_mmcsd_cmd *cmd)
{
    struct ls2k_sdio_command transfer = {0};
    struct rt_mmcsd_data *data = cmd->data;
    unsigned int response = resp_type(cmd);
    int stop_busy;

    command_fail(cmd, RT_EOK);

    /* CMD5 is SDIO I/O enumeration, not the eMMC memory initialization CMD1.
     * Reject SD probes on a dedicated eMMC port before they can change state. */
    if (cmd->cmd_code == SD_IO_SEND_OP_COND ||
        (dev->controller.emmc && (cmd->cmd_code == APP_CMD ||
                                 (cmd->cmd_code == SEND_EXT_CSD && !data))) ||
        (!dev->controller.emmc && cmd->cmd_code == SEND_OP_COND))
    {
        command_fail(cmd, -RT_ENOSYS);
        return;
    }

    if (dev->controller.emmc && cmd->cmd_code == 6)
    {
        unsigned int field = (cmd->arg >> 16) & 255U;
        unsigned int value = (cmd->arg >> 8) & 255U;
        int supported = (field == 183 && value <= 2) ||
                        (field == 185 && value <= 1) ||
                        (field == 33 && value == 0);

        /* Permit only SDR width/timing and cache disable. In particular, do
         * not expose partition/OTP programming or enable an unflushed cache. */
        /* Normal command set is 1 in both the MMC core and the Linux driver.
         * Also accept 0 (the current set), but no other set/reserved bits. */
        if (((cmd->arg >> 24) & 3U) != 3 || !supported ||
            (cmd->arg & 255U) > 1 || (cmd->arg & 0xfc000000U) || data)
        {
            command_fail(cmd, -RT_EPERM);
            return;
        }
    }

#ifndef BSP_SDIO_ALLOW_WRITE
    /* Mode selection needed by enumeration is allowed. Block payload writes,
     * erase and permanent CSD/lock programming remain disabled by default. */
    if ((data && (data->flags & DATA_DIR_WRITE)) ||
        cmd->cmd_code == 24 || cmd->cmd_code == 25 || cmd->cmd_code == 26 ||
        cmd->cmd_code == 27 || (cmd->cmd_code >= 28 && cmd->cmd_code <= 29) ||
        (cmd->cmd_code >= 32 && cmd->cmd_code <= 38) || cmd->cmd_code == 42)
    {
        command_fail(cmd, -RT_EPERM);
        return;
    }
#endif

    transfer.index = cmd->cmd_code;
    transfer.argument = cmd->arg;
    transfer.response_present = response != RESP_NONE;
    transfer.response_long = response == RESP_R2;
    transfer.response_busy = response == RESP_R1B;
    transfer.response_crc = response != RESP_NONE && response != RESP_R3 && response != RESP_R4;
    transfer.timeout_ms = data || transfer.response_busy ? 5000 : 1000;

    /* The legacy core does not initialize this optional field for CMD8/R7.
     * Only R1b commands have a busy interval to override. */
    if (transfer.response_busy && cmd->busy_timeout)
    {
        if (cmd->busy_timeout > 10000)
        {
            command_fail(cmd, -RT_EINVAL);
            return;
        }

        transfer.timeout_ms = cmd->busy_timeout;
    }

    if (data)
    {
        unsigned int direction = data->flags & (DATA_DIR_READ | DATA_DIR_WRITE);

        if (!data->buf || data->sg_len || (data->flags & DATA_STREAM) ||
            (direction != DATA_DIR_READ && direction != DATA_DIR_WRITE))
        {
            command_fail(cmd, -RT_EINVAL);
            return;
        }

        transfer.buffer = data->buf;
        transfer.blocks = data->blks;
        transfer.block_size = data->blksize;
        transfer.write = direction == DATA_DIR_WRITE;
    }

    stop_busy = transfer.response_busy && cmd->cmd_code == 12;

    if (stop_busy)
    {
        if (!dev->host->card)
        {
            command_fail(cmd, -RT_EIO);
            return;
        }

        /* CMD12 after a read may not produce a DAT0 busy edge. Complete its
         * response, then verify readiness with CMD13 instead of waiting for
         * a possibly absent busy interrupt. Keep the original R1 response. */
        transfer.response_busy = 0;
    }

    ++dev->requests;
    cmd->err = sd_error(ls2k_sdio_transfer(&dev->controller, &transfer));

    if (!cmd->err && stop_busy)
    {
        cmd->err = sd_error(ls2k_sdio_wait_ready(&dev->controller,
                                 dev->host->card->rca, transfer.timeout_ms));
    }

    rt_memcpy(cmd->resp, transfer.response, sizeof(cmd->resp));

    if (cmd->err)
    {
        ++dev->errors;
    }

    if (data)
    {
        data->err = cmd->err;
        data->bytes_xfered = cmd->err ? 0 : data->blks * data->blksize;
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

    sd->width = config->bus_width == MMCSD_BUS_WIDTH_8 ? 8 :
                config->bus_width == MMCSD_BUS_WIDTH_4 ? 4 : 1;
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

    if ((config->bus_width != MMCSD_BUS_WIDTH_1 &&
         config->bus_width != MMCSD_BUS_WIDTH_4 &&
         config->bus_width != MMCSD_BUS_WIDTH_8) || sd->width > dev->max_width)
    {
        sd->fault = 1;
        LOG_E("unsupported bus width");
        return;
    }

    /* The MMC core can request 52 MHz for eMMC SDR even with a lower
     * advertised maximum. Never exceed this board's validated clock. */
    if (config->clock > host->freq_max)
    {
        config->clock = host->freq_max;
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

static const struct rt_mmcsd_host_ops sd_ops = {
    .request = sd_request,
    .set_iocfg = sd_iocfg,
    .get_card_status = sd_card_present,
};

static unsigned int sd_port(unsigned int index)
{
#ifdef BSP_SDIO_DUAL_HOST
    return index;
#else
    RT_UNUSED(index);
    return BSP_SDIO_PORT;
#endif
}

static int sd_is_emmc(unsigned int port)
{
#ifdef BSP_SDIO_DUAL_HOST
    return port == 0;
#else
    RT_UNUSED(port);
    return 0;
#endif
}

static rt_ubase_t sd_port_base(unsigned int port)
{
    return LS2K_UNCACHED(0x16140000UL + port * 0x8000UL);
}

/* Keep each QUERY destination forever, including on timeout. The two order
 * windows can alias one engine; snapshot both before issuing either query. */
static struct ls2k_sdio_desc handoff_query[SD_HOST_COUNT][2]
    __attribute__((section(".sdio_dma"), aligned(64)));

struct sd_handoff_snapshot
{
    uint32_t low[2], high[2];
    uint32_t command, data, status;
};

static struct sd_handoff_snapshot handoff_snapshot[SD_HOST_COUNT];
static unsigned int handoff_attempted;

static int sd_handoff_query(unsigned int index, unsigned int direction)
{
    static const unsigned int orders[2] = {LSD_DMA_READ, LSD_DMA_WRITE};
    rt_ubase_t address = sd_port_base(sd_port(index)) + orders[direction];
    volatile struct ls2k_sdio_desc *query =
        (void *)LS2K_UNCACHED(&handoff_query[index][direction]);
    rt_ubase_t physical = LS2K_PHYSICAL(&handoff_query[index][direction]);
    uint32_t start = rt_tick_get();
    uint32_t state;

    if ((physical & 63U) || physical >= LS2K0300_DMA_LIMIT - sizeof(*query))
    {
        return -RT_EINVAL;
    }

    rt_memset((void *)query, 0xff, sizeof(*query));
    rt_hw_dmb();
    HWREG32(address + 4) = 0;
    HWREG32(address) = (uint32_t)physical | LSD_DMA_64BIT |
                       LSD_DMA_UNCACHED | LSD_DMA_QUERY;
    rt_hw_dmb();

    while (HWREG32(address) & LSD_DMA_QUERY)
    {
        if ((uint32_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(20))
        {
            LOG_E("SDIO%u inherited DMA query timeout", sd_port(index));
            return -RT_ETIMEOUT;
        }

        rt_thread_mdelay(1);
    }

    rt_hw_dmb();
    state = query->command;

    /* Write state 5 is the QUERY's own writeback. It is acceptable only after
     * QUERY has cleared and the sentinel really changed. DONE alone is stale
     * during a later transaction; the read/write states must also be idle. */
    if (state == UINT32_MAX || query->next == UINT32_MAX ||
        !(state & LSD_DMA_DONE) || (query->next & 1U) || (state & 0xf0U) ||
        ((state & 0xf00U) && (state & 0xf00U) != 0x500U))
    {
        LOG_E("SDIO%u inherited DMA not complete: state=%08x next=%08x",
              sd_port(index), state, query->next);
        return -RT_EBUSY;
    }

    return RT_EOK;
}

static int sd_shared_clock(uint32_t sample, uint32_t *input_clock)
{
    static const unsigned int orders[2] = {LSD_DMA_READ, LSD_DMA_WRITE};
    rt_ubase_t scale_address = LS2K_UNCACHED(LS2K0300_CLOCK_SCALE);
    uint32_t scale = HWREG32(scale_address);
    uint32_t root = BSP_SDIO_ROOT_CLOCK_HZ;
    uint32_t divider = (scale >> 24) & 15U;
    uint32_t old_divider;
    uint32_t domain_mask = 1U << 27;

    if (handoff_attempted)
    {
        /* A late DMA write must never reach reused QUERY storage. Reboot is
         * required after an attempted handoff, including a failed one. */
        return -RT_EIO;
    }

    if (!root)
    {
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

    while (root / divider > 400000U * 255U && divider < 15)
    {
        ++divider;
    }

    if (root / divider > 400000U * 255U)
    {
        return -RT_EINVAL;
    }

    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        domain_mask |= 1U << (13 + sd_port(index));
    }

#ifndef BSP_SDIO_DUAL_HOST
    if (divider != old_divider)
    {
        /* The read-only check below also needs the other register interface. */
        domain_mask |= 1U << (13 + 1U - BSP_SDIO_PORT);
    }
#endif

    /* Require a clocked, released firmware handoff before port MMIO. Manual
     * 3.3.3 warns that hot module reset can deadlock the system. Do not reset
     * a running domain or silently restart an unknown gated transaction. */
    if (!(HWREG32(LS2K_UNCACHED(0x16000424UL)) & (1U << 4)) ||
        (HWREG32(LS2K_UNCACHED(0x16000118UL)) & domain_mask) != domain_mask ||
        (HWREG32(LS2K_UNCACHED(0x16000120UL)) & domain_mask) != domain_mask)
    {
        LOG_E("SDIO firmware clock/reset handoff is not ready");
        return -RT_EBUSY;
    }

#ifndef BSP_SDIO_DUAL_HOST
    if (divider != old_divider)
    {
        rt_ubase_t other = sd_port_base(1U - BSP_SDIO_PORT);

        /* No QUERY, STOP, reset or write to the unowned port. Only the clean
         * firmware contract (unused DMA and card clock off) allows a shared
         * divider change. START clear with a programmed order is insufficient. */
        if ((HWREG32(other + LSD_CON) & LSD_CLOCK) ||
            HWREG32(other + LSD_DMA_READ) || HWREG32(other + LSD_DMA_READ + 4) ||
            HWREG32(other + LSD_DMA_WRITE) || HWREG32(other + LSD_DMA_WRITE + 4) ||
            (HWREG32(other + LSD_CMD) & LSD_CMD_START) ||
            (HWREG32(other + LSD_DATA) & LSD_DATA_START) ||
            (HWREG32(other + LSD_DATA_STATUS) & 3U))
        {
            LOG_E("shared SDIO divider requires a clean unused other port");
            return -RT_EBUSY;
        }
    }
#endif

    /* Capture every owned window before QUERY changes a mirrored order. */
    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        rt_ubase_t base = sd_port_base(sd_port(index));
        struct sd_handoff_snapshot *snapshot = &handoff_snapshot[index];

        snapshot->command = HWREG32(base + LSD_CMD);
        snapshot->data = HWREG32(base + LSD_DATA);
        snapshot->status = HWREG32(base + LSD_DATA_STATUS);

        for (unsigned int direction = 0; direction < 2; ++direction)
        {
            snapshot->low[direction] = HWREG32(base + orders[direction]);
            snapshot->high[direction] = HWREG32(base + orders[direction] + 4);
        }
    }

    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        const struct sd_handoff_snapshot *snapshot = &handoff_snapshot[index];

        if ((snapshot->command & LSD_CMD_START) ||
            (snapshot->data & LSD_DATA_START) || (snapshot->status & 3U))
        {
            LOG_E("SDIO%u has an active firmware card transfer", sd_port(index));
            return -RT_EBUSY;
        }

        for (unsigned int direction = 0; direction < 2; ++direction)
        {
            uint32_t low = snapshot->low[direction];
            uint32_t high = snapshot->high[direction];

            if (low == UINT32_MAX || high == UINT32_MAX ||
                (low & (LSD_DMA_START | LSD_DMA_QUERY | LSD_DMA_STOP)) ||
                (low && !(low & ~31U) && !high) ||
                (high && !(low & LSD_DMA_64BIT)))
            {
                LOG_E("SDIO%u has inconsistent firmware DMA order", sd_port(index));
                return -RT_EBUSY;
            }
        }
    }

    handoff_attempted = 1;

    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        for (unsigned int direction = 0; direction < 2; ++direction)
        {
            const struct sd_handoff_snapshot *snapshot = &handoff_snapshot[index];
            int result;

            if (!snapshot->low[direction] && !snapshot->high[direction])
            {
                /* Board firmware contract: an all-zero order with idle card
                 * state means an unused channel. It is NOT a general hardware
                 * proof of no DMA; never QUERY such a never-started channel. */
                continue;
            }

            result = sd_handoff_query(index, direction);

            if (result)
            {
                return result;
            }
        }
    }

    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        rt_ubase_t base = sd_port_base(sd_port(index));

        HWREG32(base + LSD_IRQ_ENABLE) = 0;
        HWREG32(base + LSD_CON) &= ~LSD_CLOCK;
    }

    rt_hw_dmb();

    if (divider != old_divider)
    {
        HWREG32(scale_address) = (scale & ~(15U << 24)) | (divider << 24);
        rt_hw_dmb();
    }

    uint32_t read_divider = (HWREG32(scale_address) >> 24) & 15U;

    if ((read_divider ? read_divider : 1U) != divider)
    {
        return -RT_EIO;
    }

    *input_clock = root / divider;
    return RT_EOK;
}

static int sd_platform_init(struct ls2k_sd_host *dev, uint32_t sample,
                            uint32_t input_clock)
{
    unsigned int port = dev->port;
    unsigned int strap_emmc = (sample >> (3 + port)) & 1U;
    rt_ubase_t mux;
    uint32_t mask;

    if (!port && !dev->controller.emmc && (sample & (1U << 6)))
    {
        LOG_E("SDIO0 is strapped for 1.8 V; a 3.3 V SD card is required");
        return -RT_EINVAL;
    }

    HWREG32(LS2K_UNCACHED(0x16000424UL)) |= 1U << 4;
    HWREG32(LS2K_UNCACHED(0x16000118UL)) |= 1U << (13 + port);
    HWREG32(LS2K_UNCACHED(0x16000120UL)) |= 1U << (13 + port);

    mux = LS2K_UNCACHED(port ? 0x160004a8UL : 0x160004a4UL);
    mask = port ? 0x000fff00U : 0xfff00000U;
    HWREG32(mux) = (HWREG32(mux) & ~mask) | mask;

    /* The native eMMC IP exposes eight data pins. A strap-selected SD IP
     * switched to eMMC remains limited to four (manual tables 15-45/16-45). */
    dev->max_width = dev->controller.emmc && strap_emmc ? 8 : 4;

    if (dev->max_width == 8 && !port)
    {
        mux = LS2K_UNCACHED(0x160004a8UL);
        HWREG32(mux) = (HWREG32(mux) & ~0xffU) | 0xffU;
    }

    dev->base = sd_port_base(port);
    dev->irq = port ? 32 : 31;
    dev->controller.base_address = LS2K_PHYSICAL(dev->base);
    dev->controller.input_clock = input_clock;
    /* BUS_SEL toggles the strap-selected IP; DATA_MODE stays SDR. */
    HWREG32(dev->base + LSD_BUS_MODE) =
        (HWREG32(dev->base + LSD_BUS_MODE) & ~3U) |
        ((strap_emmc ^ !!dev->controller.emmc) << 1);
    rt_hw_dmb();
    return RT_EOK;
}

static int sd_host_init(unsigned int index, uint32_t sample, uint32_t input_clock)
{
    struct ls2k_sd_host *dev = &devices[index];
    struct rt_mmcsd_host *host;
    char name[RT_NAME_MAX];
    int result;

    dev->port = sd_port(index);
    dev->controller.emmc = sd_is_emmc(dev->port);
    result = sd_platform_init(dev, sample, input_clock);

    if (result)
    {
        return result;
    }

    rt_spin_lock_init(&dev->lock);
    rt_snprintf(name, sizeof(name), "sd%u_irq", dev->port);
    rt_sem_init(&dev->event, name, 0, RT_IPC_FLAG_PRIO);
    dev->controller.io = &sd_io;
    dev->controller.context = dev;
    dev->controller.dma = (void *)LS2K_UNCACHED(&storage[index]);
    dev->controller.dma_address = LS2K_PHYSICAL(&storage[index]);
    dev->controller.width = 1;

    /* The shared handoff already verified inherited DMA completion. Do not
     * QUERY our controller again before its first submitted descriptor. */
    if (ls2k_sdio_reset(&dev->controller))
    {
        rt_sem_detach(&dev->event);
        return -RT_EIO;
    }

    rt_memset(dev->controller.dma, 0, sizeof(storage[index]));
    host = mmcsd_alloc_host();

    if (!host)
    {
        rt_sem_detach(&dev->event);
        return -RT_ENOMEM;
    }

    dev->host = host;
    rt_snprintf(host->name, sizeof(host->name), "mmc%u", dev->port);
    host->private_data = dev;
    host->ops = &sd_ops;
    host->freq_min = (input_clock + 254) / 255;
    host->freq_max = BSP_SDIO_MAX_CLOCK_HZ < input_clock
                         ? BSP_SDIO_MAX_CLOCK_HZ : input_clock;
    host->valid_ocr = VDD_32_33 | VDD_33_34;
    host->flags = MMCSD_BUSWIDTH_4 | MMCSD_MUTBLKWRITE;

    if (dev->controller.emmc)
    {
        host->flags |= MMCSD_SUP_NONREMOVABLE | MMCSD_SUP_HIGHSPEED;

        if (dev->max_width == 8)
        {
            host->flags |= MMCSD_BUSWIDTH_8;
        }
    }

    host->max_seg_size = LSD_BUFFER_SIZE;
    host->max_dma_segs = 1;
    host->max_blk_size = 512;
    host->max_blk_count = LSD_BUFFER_SIZE / 512;

    rt_hw_interrupt_mask(dev->irq);
    rt_hw_interrupt_install(dev->irq, sd_interrupt, dev, "sdio");
    rt_hw_interrupt_umask(dev->irq);
    LOG_I("SDIO%u %s at 0x%x irq=%d input=%u Hz, SDR <=%u Hz", dev->port,
          dev->controller.emmc ? "eMMC" : "SD", dev->controller.base_address,
          dev->irq, input_clock, host->freq_max);
    mmcsd_change(host);
    return RT_EOK;
}

static int rt_hw_sdio_init(void)
{
    uint32_t sample = HWREG32(LS2K_UNCACHED(LS2K0300_CLOCK_SAMPLE));
    uint32_t input_clock;
    int result;
    int first_error = RT_EOK;

    if (LS2K_PHYSICAL(&storage) + sizeof(storage) > LS2K0300_DMA_LIMIT)
    {
        return -RT_EINVAL;
    }

    result = sd_shared_clock(sample, &input_clock);

    if (result)
    {
        LOG_E("shared platform initialization failed (%d)", result);
        return result;
    }

    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        result = sd_host_init(index, sample, input_clock);

        if (result)
        {
            LOG_E("SDIO%u initialization failed (%d)", sd_port(index), result);

            if (!first_error)
            {
                first_error = result;
            }
        }
    }

    return first_error;
}

INIT_DEVICE_EXPORT(rt_hw_sdio_init);

#ifdef RT_USING_FINSH
static void sdio_status(void)
{
    for (unsigned int index = 0; index < SD_HOST_COUNT; ++index)
    {
        struct ls2k_sd_host *dev = &devices[index];
        rt_uint32_t interrupts;
        rt_base_t level;

        if (!dev->host)
        {
            rt_kprintf("SDIO%u host not initialized\n", sd_port(index));
            continue;
        }

        mmcsd_host_lock(dev->host);
        level = rt_spin_lock_irqsave(&dev->lock);
        interrupts = dev->interrupts;
        rt_spin_unlock_irqrestore(&dev->lock, level);
        rt_kprintf("SDIO%u %s card=%s clock=%u width=%u requests=%u errors=%u irq=%u fault=%d\n",
                   dev->port, dev->controller.emmc ? "eMMC" : "SD",
                   dev->host->card ? "present" : "absent", dev->controller.clock,
                   dev->controller.width, dev->requests, dev->errors, interrupts,
                   dev->controller.fault);
        mmcsd_host_unlock(dev->host);
    }
}

MSH_CMD_EXPORT(sdio_status, Show SDIO controller and request status);
#endif

#endif /* BSP_USING_SDIO */
