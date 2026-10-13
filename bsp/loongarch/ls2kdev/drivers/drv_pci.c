#include <rtthread.h>

#include <ls2k1000la.h>
#include <drv_pci.h>

// FIXME
rt_uint64_t pci_get_device_map_addr(rt_uint64_t bus, rt_uint64_t device, rt_uint64_t function, rt_uint64_t index)
{
    rt_uint64_t device_addr = 0xFE00000000 | (bus << 16) | ((device & 0x1f) << 11) | ((function & 0x07) << 8);
    struct pci_header *p = (struct pci_header *)(0x8000000000000000UL | device_addr);
    return 0x8000000000000000UL | ((rt_uint64_t)(p -> BaseAddressRegister[index] & 0xFFFFFFF0));
}
