#include <rtthread.h>
#include <rtdef.h>
#include <rthw.h>
#include <stdbool.h>

// lwip
#include <netif/ethernetif.h>

#include <ls2k1000la.h>
#include <drv_interrupt.h>
#include <drv_pci.h>

#include <drv_eth.h>
#include <eth_platform.h>

// default mac address
// #define DEFAULT_MAC_ADDRESS {0x00, 0x55, 0x7B, 0xB5, 0x7D, 0xF7}
#define DEFAULT_MAC_ADDRESS {0x62, 0x19, 0x1A, 0x02, 0xA8, 0x91}
// eth device name
#define ETH_DEV_NAME "e0"

struct ls_eth_device
{
    struct eth_device parent;   // rtthread

    uint8_t dev_addr[6];        // mac address
    char *name;                 // device name
    void *iobase;               // register address

    struct net_device *dev;
} ls_eth_dev;

// 获取gmac0寄存器地址
uint64_t gmac0_register_addr_init()
{
    struct pci_header *p = (struct pci_header *)(LS_PCIE_GMAC0_ADDR);
    return PHYS_TO_UNCACHED((p->BaseAddressRegister[0]) & 0xfffffff0);
}

// printf
int eth_printf(const char *fmt, ...)
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

void *plat_memcpy(void *dest, const void *src, uint64_t n)
{
    return rt_memcpy(dest, src, n);
}

uint64_t eth_malloc_align(uint64_t size, uint32_t align)
{
    return rt_malloc_align(size, align);
}

void plat_free(void *buffer)
{
    rt_free(buffer);
}

// sync all dcache data
void eth_sync_dcache()
{
    __asm__ volatile("dbar 0" ::: "memory");
}

uint32_t eth_virt_to_phys(uint64_t va)
{
    return CACHED_TO_PHYS(va);
}

uint64_t eth_phys_to_virt(uint32_t pa)
{
    return PHYS_TO_CACHED(pa);
}

uint32_t eth_handle_tx_buffer(uint64_t p, uint64_t buffer)
{
    struct pbuf *pbuf = (void *)p;
    uint32_t length = pbuf->tot_len;

    // copy pbuf to buffer
    pbuf_copy_partial(pbuf, (void *)buffer, length, 0);

    // return copy length
    return length;
}

uint64_t eth_handle_rx_buffer(uint64_t buffer, uint32_t length)
{
    struct pbuf *pbuf;

    pbuf = pbuf_alloc(PBUF_LINK, length, PBUF_RAM);
    plat_memcpy(pbuf->payload, (void *)buffer, length);

    return (uint64_t)pbuf;
}

void eth_rx_ready(struct net_device *gmacdev)
{
    // rt_kprintf("eth_rx_ready\n");
    eth_device_ready(gmacdev->parent);
    // rt_kprintf("leave eth_rx_ready\n");
}

int rt_eth_tx(rt_device_t device, struct pbuf *p)
{
    struct ls_eth_device *dev = device;
    struct net_device *gmacdev = dev->dev;
    return eth_tx(gmacdev, p);
}

struct pbuf *rt_eth_rx(rt_device_t device)
{
    struct ls_eth_device *dev = device;
    struct net_device *gmacdev = dev->dev;
    struct pbuf *pbuf = eth_rx(gmacdev);
    return pbuf;
}

void rth_eth_irq(int irq, void *param)
{
    struct ls_eth_device *dev = param;
    struct net_device *gmacdev = dev->dev;
    // rt_kprintf("eth_irq: %p\n", eth_irq);
    eth_irq(gmacdev);
    // rt_kprintf("leave eth_irq\n");
}

int rt_eth_init(rt_device_t device)
{
    struct ls_eth_device *dev = device;
    struct net_device *gmacdev = dev->dev;

    // set mac address
    for (int i = 0; i < 6; ++ i)
        gmacdev->MacAddr[i] = dev->dev_addr[i];

    // set regbase address
    gmacdev->iobase = dev->iobase;

    return eth_init(gmacdev);
}

// install eth isr
void eth_isr_register()
{
    rt_hw_interrupt_install(LS2K_GMAC0_SBD_IRQ, rth_eth_irq, &ls_eth_dev, "e0_isr");
    rt_hw_interrupt_umask(LS2K_GMAC0_SBD_IRQ);
}

void eth_isr_install()
{
    eth_isr_register();
}

// only to get mac address
rt_err_t eth_control(rt_device_t device, int cmd, void *args)
{
    struct ls_eth_device *ls_dev = device;
    switch (cmd)
    {
    case NIOCTL_GADDR:
        if (args) rt_memcpy(args, ls_dev->dev_addr, 6);
        else return -RT_ERROR;
        break;

    default :
        break;
    }
    return RT_EOK;
}

void eth_update_linkstate(struct net_device *gmacdev, uint32_t status)
{
    struct eth_device *device = gmacdev->parent;
    eth_device_linkchange(device, status);
}

static struct rt_device_ops eth_ops = {
    .init = rt_eth_init,
    .open = RT_NULL,
    .close = RT_NULL,
    .read = RT_NULL,
    .write = RT_NULL,
    .control = eth_control
};

// 最开始的初始化
// rtthread还会调用eth_init
int rt_hw_eth_init(void)
{
    // 获取gmac地址
    uint64_t gmac_base = gmac0_register_addr_init();

    // 获取配置mac地址
    uint8_t mac_addr[6] = DEFAULT_MAC_ADDRESS;

    // set ls_eth_device
    rt_memset(&ls_eth_dev, 0, sizeof(ls_eth_dev));
    ls_eth_dev.name = ETH_DEV_NAME;
    ls_eth_dev.iobase = gmac_base;

    ls_eth_dev.dev = (struct net_device *)rt_malloc(sizeof(struct net_device));
    rt_memset(ls_eth_dev.dev, 0, sizeof(struct net_device));
    ls_eth_dev.dev->parent = &ls_eth_dev;

    // set mac address
    for (int i = 0; i < 6; ++ i)
        ls_eth_dev.dev_addr[i] = mac_addr[i];

    // set tx/rx handler for eth_device
    struct eth_device *eth_dev  = &ls_eth_dev.parent;
    eth_dev->eth_tx             = rt_eth_tx;
    eth_dev->eth_rx             = rt_eth_rx;

    // set handler for rt_device
    struct rt_device *rt_dev    = &eth_dev->parent;
    rt_dev->type                = RT_Device_Class_NetIf;
    rt_dev->ops                 = &eth_ops;
    rt_dev->user_data           = RT_NULL;

    // use ls_eth_dev.parent to init eth device
    // init device
    eth_device_init(eth_dev, ETH_DEV_NAME);
    // set link down
    eth_device_linkchange(eth_dev, RT_TRUE);

    return 0;
}
INIT_COMPONENT_EXPORT(rt_hw_eth_init);
