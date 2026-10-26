/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    prepare Xingyun PHY and DMA attributes
 *
 * Description:
 *   Use firmware PCI resources and prepare the identified YT8511 PHY before
 *   MAC reset. Match DMA requests to the uncached buffers. Preserve RGMII
 *   delay settings; do not alter unknown PHYs.
 */
#include "ls2k_gmac_platform.h"
#include "drv_pci.h"
#include "platform_interrupt.h"
#include "ls2k.h"
#include <rthw.h>

#ifdef BSP_USING_GMAC
#include "gmac.h"

static uint32_t phy_read(void *context, unsigned int reg)
{
    return HWREG32((rt_ubase_t)context + reg);
}

static void phy_write(void *context, unsigned int reg, uint32_t value)
{
    HWREG32((rt_ubase_t)context + reg) = value;
    rt_hw_dmb();
}

static void phy_delay(void *context, unsigned int ms)
{
    RT_UNUSED(context);
    rt_thread_mdelay(ms);
}

static void phy_barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static const struct ls2k_gmac_io phy_io =
{
    phy_read, phy_write, phy_delay, phy_barrier
};

static int xingyun_phy_clock(rt_ubase_t base, unsigned int port)
{
    struct ls2k_gmac mac =
    {
        .io = &phy_io,
        .context = (void *)base,
        .mdio_clock = BSP_GMAC_MDIO_CLOCK,
    };
    uint16_t high, low, selector, value = 0, check = 0;
    int error, restore;
    unsigned int attempt;

    /* Both PHYs on the Xingyun board are at Clause-22 address zero. */
    error = ls2k_gmac_mdio_read(&mac, 0, 2, &high);

    if (!error)
    {
        error = ls2k_gmac_mdio_read(&mac, 0, 3, &low);
    }

    if (error || high != 0 || low != 0x010a)
    {
        return error;
    }

    error = ls2k_gmac_mdio_read(&mac, 0, 0x1e, &selector);

    if (error)
    {
        return error;
    }

    /* YT8511 datasheet EXT 27H: clear En_sleep_sw (bit 15), then wait
     * until Sleeping (bit 5) clears. Keeping PLL alone does not wake this
     * board's unplugged PHY, and the MAC reset still times out. Preserve
     * the RGMII delay and clock-output selection registers completely. */
    error = ls2k_gmac_mdio_write(&mac, 0, 0x1e, 0x27);

    if (!error)
    {
        error = ls2k_gmac_mdio_read(&mac, 0, 0x1f, &value);
    }

    if (!error)
    {
        error = ls2k_gmac_mdio_write(&mac, 0, 0x1f, value & ~(1U << 15));
    }

    for (attempt = 0; !error && attempt < 200; attempt++)
    {
        error = ls2k_gmac_mdio_read(&mac, 0, 0x1f, &check);

        if (!error && !(check & ((1U << 15) | (1U << 5))))
        {
            break;
        }

        rt_thread_mdelay(1);
    }

    if (!error && (check & ((1U << 15) | (1U << 5))))
    {
        error = -RT_ETIMEOUT;
    }

    restore = ls2k_gmac_mdio_write(&mac, 0, 0x1e, selector);

    if (!error)
    {
        error = restore;
    }

    rt_kprintf("Xingyun e%u: YT8511 sleep clock %04x -> %04x, result=%d\n",
               port, value, check, error);
    rt_thread_mdelay(1);
    return error;
}
#endif

rt_err_t ls2k_gmac_platform_init(unsigned int port, rt_ubase_t *base, int *irq)
{
    rt_err_t error;
    rt_base_t level;
    rt_uint32_t coherence, check;

    if (port > 1 || !base || !irq)
    {
        return -RT_EINVAL;
    }

    *irq = port ? LS2K_GMAC1_SBD_IRQ : LS2K_GMAC0_SBD_IRQ;
    error = ls2k_pci_enable_gmac(port, base);

    if (error)
    {
        return error;
    }

    /* The common driver accesses its DMA storage only through SUC aliases.
     * Match that with noncoherent DDR requests before either MAC starts.
     * LS2K1000LA user manual 5.1: CFG0 bit 0 is gmac_coherence_en;
     * all other bits include unrelated pin mux and device controls. */
    level = rt_hw_interrupt_disable();
    coherence = HWREG32(LS2K_UNCACHED(0x1fe00420UL));
    HWREG32(LS2K_UNCACHED(0x1fe00420UL)) = coherence & ~1U;
    __asm__ volatile("dbar 0" ::: "memory");
    check = HWREG32(LS2K_UNCACHED(0x1fe00420UL));
    rt_hw_interrupt_enable(level);

    if (check & 1U)
    {
        rt_kprintf("Xingyun e%u: cannot disable coherent DMA (%08x)\n", port, check);
        return -RT_ERROR;
    }

#ifdef BSP_GMAC_USING_ECHO_TEST
    if (port == 0)
    {
        rt_kprintf("Xingyun DMA coherence=%08x -> %08x gmac=%u\n",
                   coherence, check, check & 1U);
    }
#endif

#ifdef BSP_USING_GMAC
    if (!error)
    {
        error = xingyun_phy_clock(*base, port);
    }
#endif

    return error;
}

rt_ubase_t ls2k_gmac_dma_limit(void)
{
    return 1UL << 32;
}
