#include <rtthread.h>
#include <rthw.h>
#include <rtconfig.h>
#include <drivers/blk.h>

#include <loongarch.h>
#include <ls2k1000la.h>
#include <drv_pci.h>

#include <ahci_platform.h>
#include <drv_ahci.h>

struct rt_ahci_device
{
    struct rt_device parent;
    struct ahci_device ahci_dev;
};

void ahci_mdelay(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

int ahci_printf(const char *fmt, ...)
{
    va_list args;
    int length = 0;
    static char rt_log_buf[RT_CONSOLEBUF_SIZE];

    va_start(args, fmt);

    length = rt_vsnprintf(rt_log_buf, sizeof(rt_log_buf) - 1, fmt, args);
    if (length > RT_CONSOLEBUF_SIZE - 1)
    {
        length = RT_CONSOLEBUF_SIZE - 1;
    }

    rt_kputs(rt_log_buf);

    va_end(args);

    return length;
}

void *ahci_memset(void *s, int c, uint64_t count)
{
    return rt_memset(s, c, count);
}

void *ahci_memcpy(void *dest, const void *src, uint64_t n)
{
    return rt_memcpy(dest, src, n);
}

uint64_t ahci_malloc_align(uint64_t size, uint32_t align)
{
    return (uint64_t)rt_malloc_align(size, align);
}

// sync all dcache data
void ahci_sync_dcache()
{
    __asm__ volatile("dbar 0" ::: "memory");
}

uint64_t ahci_virt_to_phys(uint64_t va)
{
    return CACHED_TO_PHYS(va);
}

uint64_t ahci_phys_to_uncached(uint64_t va)
{
    return PHYS_TO_UNCACHED(va);
}

// disk read
rt_ssize_t rt_ahci_sata_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_uint64_t size)
{
    struct rt_ahci_device *rt_ahci_dev = dev;
    struct ahci_device *ahci_dev = &rt_ahci_dev->ahci_dev;
    return (uint64_t)ahci_sata_read_common(ahci_dev, pos, size, buffer);
}

// disk write
rt_ssize_t rt_ahci_sata_write(rt_device_t dev, rt_off_t pos, void *buffer, rt_uint64_t size)
{
    struct rt_ahci_device *rt_ahci_dev = dev;
    struct ahci_device *ahci_dev = &rt_ahci_dev->ahci_dev;
    return (uint64_t)ahci_sata_write_common(ahci_dev, pos, size, buffer);
}

rt_err_t rt_ahci_sata_control(rt_device_t dev, int cmd, void *args)
{
    struct rt_ahci_device *rt_ahci_dev = dev;
    struct ahci_device *ahci_dev = &rt_ahci_dev->ahci_dev;
    struct ahci_blk_dev *blk = &ahci_dev->blk_dev;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_BLK_GETGEOME:
        if (args != NULL)
        {
            struct rt_device_blk_geometry *info = args;
            info->sector_count = blk->lba;
            info->bytes_per_sector = blk->blksz;
            info->block_size = blk->blksz;
        }
        break;

    default:
        rt_kprintf("[rt_ahci_sata_control] Unimplemented cmd\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}

static struct rt_device_ops ahsata_scan_ops = {
    .init = RT_NULL,
    .open = RT_NULL,
    .close = RT_NULL,
    .control = rt_ahci_sata_control,
    .read = rt_ahci_sata_read,
    .write = rt_ahci_sata_write
};

// 初始化时调用
// 初始化ahci硬盘控制器，并注册相关的设备
// ls_ahci_blk，硬盘块设备本体
int rt_hw_ahci_host_init()
{
    struct pci_header *p = (struct pci_header *)(LS_PCIE_SATA_ADDR);
    rt_uint64_t achi_base_phys = (p->BaseAddressRegister[0]) & 0xfffffff0;
    rt_uint64_t ahci_base = PHYS_TO_UNCACHED(achi_base_phys);

    rt_err_t ret;
    struct rt_ahci_device *rt_ahci_dev;
    struct ahci_device *ahci_dev;

    // create device
    rt_ahci_dev = rt_device_create(RT_Device_Class_Block,
            sizeof(struct rt_ahci_device) - sizeof(struct rt_device));
    rt_ahci_dev->parent.ops = &ahsata_scan_ops;

    ahci_dev = &rt_ahci_dev->ahci_dev;

    if (ahci_init(ahci_dev))
    {
        rt_kprintf("ahci failed to init\n");
        return -RT_ERROR;
    }

    ret = rt_device_register(rt_ahci_dev, "ls_ahci_blk", RT_DEVICE_FLAG_RDWR);
    if (ret)
    {
        rt_kprintf("cannot create device ls_ahci_blk\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_COMPONENT_EXPORT(rt_hw_ahci_host_init);
