/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Register independent native SD and eMMC hosts without changing MMC/SD core.
 *   Firmware retains pad mux and supply ownership. No voltage switching or DDR
 *   mode is advertised. Requests use bounded polling until IRQ routing is tested.
 */

#include <rthw.h>
#include <drivers/dev_mmcsd_core.h>
#include "board.h"
#include "ls2k3000_pci.h"
#include "mmc_poll.h"

#define MMC_HOST_COUNT 3
#define MMC_PCI_EMMC   0x7a880014U
#define MMC_PCI_SD     0x7a480014U

struct native_mmc
{
    struct ls2k3000_mmc controller;
    struct rt_mmcsd_host *host;
    const struct ls2k3000_pci_device *pci;
    rt_ubase_t base;
    uint32_t requests;
    uint32_t errors;
    uint32_t last_command;
    uint32_t last_argument;
    int last_error;
};

static struct native_mmc devices[MMC_HOST_COUNT];
static unsigned int device_count;

static uint32_t native_read(void *context, unsigned int offset)
{
    struct native_mmc *dev = context;
    uint32_t value = HWREG32(dev->base + offset);

    rt_hw_dmb();
    return value;
}

static void native_write(void *context, unsigned int offset, uint32_t value)
{
    struct native_mmc *dev = context;

    HWREG32(dev->base + offset) = value;
    rt_hw_dmb();
}

static uint32_t native_time(void *context)
{
    RT_UNUSED(context);
    return (uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND;
}

static void native_wait(void *context, unsigned int milliseconds)
{
    RT_UNUSED(context);
    rt_thread_mdelay(milliseconds);
}

static void native_barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static const struct ls2k3000_mmc_io native_io =
{
    .read = native_read,
    .write = native_write,
    .time_ms = native_time,
    .wait_ms = native_wait,
    .barrier = native_barrier,
};

static int native_error(int result)
{
    switch (result)
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

static void native_command(struct native_mmc *dev, struct rt_mmcsd_cmd *cmd)
{
    struct ls2k3000_mmc_command transfer = {0};
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
        if (((cmd->arg >> 24) & 3U) != 3 || !supported || (cmd->arg & 255U) > 1)
        {
            command_fail(cmd, -RT_EPERM);
            return;
        }
    }

#ifndef BSP_LS2K3000_MMC_ALLOW_WRITE
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
    dev->last_command = cmd->cmd_code;
    dev->last_argument = cmd->arg;
    cmd->err = native_error(ls2k3000_mmc_transfer(&dev->controller, &transfer));

    if (!cmd->err && stop_busy)
    {
        cmd->err = native_error(ls2k3000_mmc_wait_ready(&dev->controller,
                                 dev->host->card->rca, transfer.timeout_ms));
    }

    dev->last_error = cmd->err;
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

static void native_request(struct rt_mmcsd_host *host, struct rt_mmcsd_req *request)
{
    struct native_mmc *dev = host->private_data;

    /* The MMC core holds bus_lock across this synchronous request and stop. */
    if (request->sbc)
    {
        native_command(dev, request->sbc);

        if (request->sbc->err)
        {
            command_fail(request->cmd, request->sbc->err);
            mmcsd_req_complete(host);
            return;
        }
    }

    native_command(dev, request->cmd);

    if (request->stop)
    {
        if (dev->controller.fault)
        {
            command_fail(request->stop, -RT_EIO);
        }
        else
        {
            native_command(dev, request->stop);
        }

        if (request->stop->err && request->data)
        {
            request->data->err = request->stop->err;
            request->data->bytes_xfered = 0;
        }
    }

    mmcsd_req_complete(host);
}

static void native_iocfg(struct rt_mmcsd_host *host, struct rt_mmcsd_io_cfg *config)
{
    struct native_mmc *dev = host->private_data;
    struct ls2k3000_mmc *controller = &dev->controller;

    if (controller->fault)
    {
        return;
    }

    if (config->power_mode == MMCSD_POWER_OFF)
    {
        native_write(dev, LSD_IRQ_ENABLE, 0);
        ls2k3000_mmc_clock(controller, 0);
        return;
    }

    if ((config->timing != MMCSD_TIMING_LEGACY &&
         !(controller->emmc && config->timing == MMCSD_TIMING_MMC_HS)) ||
        (config->bus_width != MMCSD_BUS_WIDTH_1 &&
         config->bus_width != MMCSD_BUS_WIDTH_4 &&
         !(controller->emmc && config->bus_width == MMCSD_BUS_WIDTH_8)))
    {
        controller->fault = 1;
        rt_kprintf("MMC: unsupported timing or bus width\n");
        return;
    }

    controller->width = config->bus_width == MMCSD_BUS_WIDTH_8 ? 8 :
                        config->bus_width == MMCSD_BUS_WIDTH_4 ? 4 : 1;

    /* The current MMC core requests 52 MHz for ordinary eMMC SDR without
     * clamping to freq_max. Keep that policy local to this board driver. */
    if (config->clock > host->freq_max)
    {
        config->clock = host->freq_max;
    }

    if ((config->power_mode == MMCSD_POWER_UP && ls2k3000_mmc_reset(controller)) ||
        ls2k3000_mmc_clock(controller, config->clock))
    {
        controller->fault = 1;
        rt_kprintf("MMC: reset/clock failed (%u Hz)\n", config->clock);
    }
}

static rt_int32_t native_present(struct rt_mmcsd_host *host)
{
    RT_UNUSED(host);
    /* The Explorer CD GPIO has not been verified. Insert SD before boot;
     * manual rescan is allowed only while this host has no registered card. */
    return 1;
}

static const struct rt_mmcsd_host_ops native_ops =
{
    .request = native_request,
    .set_iocfg = native_iocfg,
    .get_card_status = native_present,
};

static int register_host(uint64_t address, int emmc,
                         const struct ls2k3000_pci_device *pci)
{
    struct native_mmc *dev;
    struct ls2k3000_mmc *controller;
    struct rt_mmcsd_host *host;
    uint64_t physical;

    if (!address || (address & 0xfff) || address > UINT32_MAX - 0x1000U ||
        device_count == MMC_HOST_COUNT)
    {
        return -RT_EINVAL;
    }

    for (unsigned int index = 0; index < device_count; ++index)
    {
        if (devices[index].controller.base_address == address)
        {
            return -RT_EBUSY;
        }
    }

    dev = &devices[device_count];
    controller = &dev->controller;
    dev->base = LOONGARCH_PHYS_UNCACHED(address);
    dev->pci = pci;
    controller->io = &native_io;
    controller->context = dev;
    controller->base_address = address;
    controller->input_clock = BSP_LS2K3000_MMC_INPUT_HZ;
    controller->width = 1;
    controller->emmc = emmc;

    /* Stop inherited DMA independently of the command engine before reset.
     * A failed stop/reset leaves this host offline and no pool storage reused. */
    native_write(dev, LSD_DMA_WRITE + 4, 0);
    native_write(dev, LSD_DMA_WRITE, LSD_DMA_STOP);
    native_write(dev, LSD_DMA_READ + 4, 0);
    native_write(dev, LSD_DMA_READ, LSD_DMA_STOP);
    rt_thread_mdelay(1);

    if (ls2k3000_mmc_reset(controller))
    {
        return -RT_EIO;
    }

    native_write(dev, LSD_BUS_MODE, native_read(dev, LSD_BUS_MODE) & ~3U);
    controller->dma = ls2k3000_dma_alloc(sizeof(*controller->dma), 64, &physical);

    if (!controller->dma || physical > 0x10000000U - sizeof(*controller->dma))
    {
        return -RT_ENOMEM;
    }

    controller->dma_address = physical;
    rt_memset(controller->dma, 0, sizeof(*controller->dma));
    if (pci && ls2k3000_pci_command(pci, 6, 0))
    {
        controller->fault = 1;
        ls2k3000_pci_command(pci, 0, 4);

        return -RT_EIO;
    }

    /* On Explorer, QUERY before the first descriptor leaves START pending
     * indefinitely. Stop the inherited channels without an initial QUERY.
     * Request completion/error paths still query and confirm DMA ownership
     * before copying data or reusing buffers submitted by this driver. */
    for (unsigned int channel = 0; channel < 2; ++channel)
    {
        unsigned int reg = channel ? LSD_DMA_READ : LSD_DMA_WRITE;

        native_barrier(dev);
        native_write(dev, reg + 4, 0);
        native_write(dev, reg, controller->dma_address | LSD_DMA_64BIT | LSD_DMA_STOP);
        rt_thread_mdelay(1);
    }

    host = mmcsd_alloc_host();

    if (!host)
    {
        return -RT_ENOMEM;
    }

    /* The non-DM core defaults every host to "sd". Each physical disk and
     * its partitions need a distinct name when eMMC and SD are both online. */
    rt_snprintf(host->name, sizeof(host->name), "mmc%u", device_count);
    host->private_data = dev;
    host->ops = &native_ops;
    host->freq_min = (controller->input_clock + 1022U) / 1023U;
    host->freq_max = BSP_LS2K3000_MMC_MAX_HZ;
    host->valid_ocr = VDD_32_33 | VDD_33_34;
    host->flags = MMCSD_BUSWIDTH_4 | MMCSD_MUTBLKWRITE;

    if (emmc)
    {
        host->flags |= MMCSD_BUSWIDTH_8 | MMCSD_SUP_NONREMOVABLE | MMCSD_SUP_HIGHSPEED;
    }

    host->max_seg_size = LSD_BUFFER_SIZE;
    host->max_dma_segs = 1;
    host->max_blk_size = 512;
    host->max_blk_count = LSD_BUFFER_SIZE / 512;
    dev->host = host;
    ++device_count;
    rt_kprintf("MMC host %u: %s base=%x input=%u max=%u Hz (SDR)\n",
               device_count - 1, emmc ? "eMMC" : "SD", controller->base_address,
               controller->input_clock, host->freq_max);
    mmcsd_change(host);
    return RT_EOK;
}

static int native_mmc_init(void)
{
    unsigned int matching = 0;
    int result;

    /* The input is a firmware clock, not the CPU counter or UART frequency.
     * Until the PLL path is verified, require an explicit board value. */
    if (!BSP_LS2K3000_MMC_INPUT_HZ ||
        BSP_LS2K3000_MMC_INPUT_HZ > 400000U * 1023U ||
        BSP_LS2K3000_MMC_INPUT_HZ < BSP_LS2K3000_MMC_MAX_HZ)
    {
        rt_kprintf("MMC: configure verified BSP_LS2K3000_MMC_INPUT_HZ first\n");
        return -RT_EINVAL;
    }

    for (uint32_t index = 0; index < ls2k3000_boot->pci_count; ++index)
    {
        const struct ls2k3000_pci_device *pci = &ls2k3000_boot->pci[index];
        rt_ubase_t base;

        if (pci->id != MMC_PCI_EMMC && pci->id != MMC_PCI_SD)
        {
            continue;
        }

        ++matching;

        if (ls2k3000_pci_map(pci, 0, 0x1000, &base) ||
            ls2k3000_pci_command(pci, 2, 4))
        {
            rt_kprintf("MMC: invalid firmware PCI resource\n");
            continue;
        }

        result = register_host(pci->bar[0].base, pci->id == MMC_PCI_EMMC, pci);

        if (result)
        {
            rt_kprintf("MMC: PCI host setup failed (%d)\n", result);
        }
    }

#ifdef BSP_LS2K3000_MMC_ACPI_STATIC
    /* This explicit fallback is for firmware hiding D28 from PCI enumeration.
     * Addresses must come from this board's ACPI _CRS, never guessed BARs. */
    if (!matching)
    {
        if (BSP_LS2K3000_EMMC_BASE)
        {
            result = register_host(BSP_LS2K3000_EMMC_BASE, 1, RT_NULL);
            rt_kprintf("MMC: ACPI eMMC registration=%d\n", result);
        }

        if (BSP_LS2K3000_SD_BASE)
        {
            result = register_host(BSP_LS2K3000_SD_BASE, 0, RT_NULL);
            rt_kprintf("MMC: ACPI SD registration=%d\n", result);
        }
    }
#else
    RT_UNUSED(matching);
#endif

    return device_count ? RT_EOK : -RT_ENOSYS;
}
INIT_DEVICE_EXPORT(native_mmc_init);

#ifdef RT_USING_FINSH
static void ls2k3000_mmc_status(void)
{
    for (unsigned int index = 0; index < device_count; ++index)
    {
        struct native_mmc *dev = &devices[index];

        mmcsd_host_lock(dev->host);
        rt_kprintf("MMC%u %s card=%s clock=%u width=%u req=%u err=%u last=%u/%d fault=%d\n",
                   index, dev->controller.emmc ? "eMMC" : "SD",
                   dev->host->card ? "present" : "absent", dev->controller.clock,
                   dev->controller.width, dev->requests, dev->errors,
                   dev->last_command, dev->last_error, dev->controller.fault);
        rt_kprintf("MMC registers: argument=%08x response=%08x command=%08x status=%08x dma=%08x\n",
                   dev->last_argument, dev->controller.last_response,
                   dev->controller.last_command_status, dev->controller.last_status,
                   dev->controller.last_dma_state);
        rt_kprintf("MMC data: control=%08x status=%08x fifo=%08x order=%08x\n",
                   dev->controller.last_data_control, dev->controller.last_data_status,
                   dev->controller.last_fifo_status, dev->controller.last_dma_order);
        mmcsd_host_unlock(dev->host);
    }

    if (!device_count)
    {
        rt_kprintf("MMC: no initialized host\n");
    }
}
MSH_CMD_EXPORT(ls2k3000_mmc_status, Show native SD/eMMC host state);

static void ls2k3000_mmc_rescan(int argc, char **argv)
{
    struct native_mmc *dev;
    unsigned int index;
    int scan;

    if (argc != 2 || argv[1][0] < '0' || argv[1][0] > '2' || argv[1][1])
    {
        rt_kprintf("Usage: ls2k3000_mmc_rescan <0..2> (insert card first)\n");
        return;
    }

    index = argv[1][0] - '0';

    if (index >= device_count)
    {
        return;
    }

    dev = &devices[index];
    mmcsd_host_lock(dev->host);
    scan = !dev->host->card && !dev->controller.fault;
    mmcsd_host_unlock(dev->host);

    if (scan)
    {
        mmcsd_change(dev->host);
    }
    else
    {
        rt_kprintf("MMC: rescan refused for attached or faulted host\n");
    }
}
MSH_CMD_EXPORT(ls2k3000_mmc_rescan, Rescan an empty nonfaulted memory card host);
#endif
