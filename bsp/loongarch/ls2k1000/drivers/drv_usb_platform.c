/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    share EHCI through board resources
 *
 */

/*
 * Description:
 *   Enable the firmware-assigned LS2K1000LA PCI EHCI function. Expose only
 *   root port 1 through the shared single-port HCD. Keep the companion out of
 *   suspend without enabling its DMA schedules; OTG is not initialized.
 */
#include <rtthread.h>
#include <rthw.h>

#ifdef BSP_USING_USB_HOST
#include "ls2k1000la.h"
#include "drv_pci.h"
#include "platform_interrupt.h"
#include "ls2k_usb_platform.h"

rt_err_t ls2k_usb_platform_init(rt_ubase_t *base, int *irq,
                               unsigned int *hardware_ports)
{
    rt_ubase_t config = LS_PCIE_USB_EHCI_ADDR;
    rt_uint16_t command;
    rt_ubase_t companion;
    rt_uint32_t control;

    /* Reject a different PCI function instead of programming guessed MMIO. */
    if ((HWREG32(config) & 0xffff) != 0x0014 ||
        (HWREG32(config + 8) >> 8) != 0x0c0320)
    {
        return -RT_ENOSYS;
    }

    *base = pci_get_device_map_addr(0, 4, 1, 0);

    if (!*base)
    {
        return -RT_ERROR;
    }

    /* A halfword access preserves the adjacent write-one-to-clear status. */
    command = HWREG16(config + 4);
    HWREG16(config + 4) = (command | 6U) & ~(1U << 10);
    rt_hw_dmb();

    if ((HWREG16(config + 4) & 6U) != 6U)
    {
        return -RT_ERROR;
    }

    /* U-Boot usb stop leaves the companion in USB Suspend (HCFS=3).
     * On this silicon EHCI then reports CCS=0 even with port power enabled.
     * USB Reset (HCFS=0) restores detection, with every OHCI DMA schedule
     * disabled. This does not provide a low/full-speed OHCI driver. */
    if ((HWREG32(LS_PCIE_USB_OHCI_ADDR) & 0xffff) != 0x0014 ||
        (HWREG32(LS_PCIE_USB_OHCI_ADDR + 8) >> 8) != 0x0c0310)
    {
        return -RT_ENOSYS;
    }

    companion = pci_get_device_map_addr(0, 4, 2, 0);

    if (!companion)
    {
        return -RT_ERROR;
    }

    rt_hw_interrupt_mask(LS2K_OHCI_IRQ);
    HWREG32(companion + 0x14) = 0xffffffffU; /* HcInterruptDisable */
    control = HWREG32(companion + 4);
    HWREG32(companion + 4) = control & ~0xfcU; /* HCFS and all list enables */
    rt_hw_dmb();

    if (HWREG32(companion + 4) & 0xfcU)
    {
        return -RT_ERROR;
    }

    rt_thread_mdelay(20);
    rt_kprintf("USB companion: control %08x -> %08x, schedules disabled\n",
               control, HWREG32(companion + 4));
    *irq = LS2K_EHCI_IRQ;
    *hardware_ports = 3;
    return RT_EOK;
}
#endif
