#include <rtthread.h>
#include <rthw.h>
#include "rtcompiler.h"
#include "rttypes.h"

#include <drivers/dev_pin.h>
#include <loongarch.h>
#include <ls2k1000la.h>
#include <drv_interrupt.h>

rt_always_inline rt_uint32_t fls64(rt_uint64_t word)
{
	rt_uint32_t num = 63;
    if (word == 0) {
        return 0;
    }
	if (!(word & (~0ul << 32))) {
		num -= 32;
		word <<= 32;
	}
	if (!(word & (~0ul << 48))) {
		num -= 16;
		word <<= 16;
	}
	if (!(word & (~0ul << 56))) {
		num -= 8;
		word <<= 8;
	}
	if (!(word & (~0ul << 60))) {
		num -= 4;
		word <<= 4;
	}
	if (!(word & (~0ul << 62))) {
		num -= 2;
		word <<= 2;
	}
	if (!(word & (~0ul << 63)))
		num -= 1;
	return num + 1;
}

static struct rt_irq_desc irq_handle_table[MAX_INTR];

// mask interrupt
void rt_hw_interrupt_mask(int irq)
{
    HWREG32(LS2K_INTENCLR_REG(irq)) |= (1 << (irq % 32));
}

// unmask interrupt
void rt_hw_interrupt_umask(int irq)
{
    HWREG32(LS2K_INTENSET_REG(irq)) |= (1 << (irq % 32));
}

// default interrupt handler
static void rt_hw_interrupt_handler(int irq, void *param)
{
    rt_kprintf("Unhandled interrupt %d occured!!!\n", irq);
}

// init liointc
static void liointc_init(void)
{
    // all route to (Core 0, INT0)
    for (rt_uint32_t i = 0; i < 64; i ++)
    {
        HWREG8(LS2K_IRQ_ROUTE_REG(i)) = LIOINTC_COREx_INTy(0, 0);
    }

    // disable all IRQs
    HWREG32(LIOINTC0_BASE + LIOINTC_REG_INTC_DISABLE) = 0xffffffff;
    HWREG32(LIOINTC1_BASE + LIOINTC_REG_INTC_DISABLE) = 0xffffffff;

    // set all IRQs to low level triggered
    HWREG32(LIOINTC0_BASE + LIOINTC_REG_INTC_POL) = 0x0;
    HWREG32(LIOINTC1_BASE + LIOINTC_REG_INTC_POL) = 0x0;
    HWREG32(LIOINTC0_BASE + LIOINTC_REG_INTC_EDGE) = 0x0;
    HWREG32(LIOINTC1_BASE + LIOINTC_REG_INTC_EDGE) = 0x0;

    // set all auto and bounce to 0
    HWREG32(LIOINTC0_BASE + LIOINTC_REG_BOUNCE) = 0x0;
    HWREG32(LIOINTC1_BASE + LIOINTC_REG_BOUNCE) = 0x0;
    HWREG32(LIOINTC0_BASE + LIOINTC_REG_INTC_AUTO) = 0x0;
    HWREG32(LIOINTC1_BASE + LIOINTC_REG_INTC_AUTO) = 0x0;
}

// set pol and edge
void liointc_set_irq_mode(int irq, int mode)
{
    rt_uint32_t pol_val, edge_val;
    pol_val = HWREG32(LS2K_INTPOL_REG(irq));
    edge_val = HWREG32(LS2K_INTEDGE_REG(irq));

    switch (mode)
    {
    case PIN_IRQ_MODE_RISING:
        edge_val |= (1 << (irq % 32));
        pol_val &= ~(1 << (irq % 32));
        break;
    case PIN_IRQ_MODE_FALLING:
        edge_val |= (1 << (irq % 32));
        pol_val |= (1 << (irq % 32));
        break;
    case PIN_IRQ_MODE_HIGH_LEVEL:
        edge_val &= ~(1 << (irq % 32));
        pol_val |= (1 << (irq % 32));
        break;
    case PIN_IRQ_MODE_LOW_LEVEL:
        edge_val &= ~(1 << (irq % 32));
        pol_val &= ~(1 << (irq % 32));
        break;
    default:
        edge_val |= (1 << (irq % 32));
        pol_val |= (1 << (irq % 32));
        break;
    }

    HWREG32(LS2K_INTPOL_REG(irq)) = pol_val;
    HWREG32(LS2K_INTEDGE_REG(irq)) = edge_val;
}

rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
        void *param, const char *name)
{
    rt_isr_handler_t old_handler = RT_NULL;

    if (vector >= 0 && vector < MAX_INTR)
    {
        old_handler = irq_handle_table[vector].handler;
        if (handler != RT_NULL)
        {
            irq_handle_table[vector].handler = handler;
            irq_handle_table[vector].param = param;
#ifdef RT_USING_INTERRUPT_INFO
            rt_strncpy(irq_handle_table[vector].name, name, RT_NAME_MAX);
#endif /* RT_USING_INTERRUPT_INFO */
        }
    }

    return old_handler;
}

void platform_generic_irq()
{
    rt_uint64_t pending;
    rt_uint32_t index;

    // only handle CORE0 for now
    pending = HWREG32(LS2K_COREISR_REG1(0));
    pending <<= 32;
    pending |= HWREG32(LS2K_COREISR_REG0(0));

    while (pending)
    {
        rt_uint32_t bit = fls64(pending);
        index = bit - 1;
        rt_hw_interrupt_mask(index);
        irq_handle_table[index].handler(index, irq_handle_table[index].param);
        pending &= ~(1UL << index);
        rt_hw_interrupt_umask(index);
    }
}

void rt_hw_interrupt_init()
{
    rt_uint32_t idx;

    rt_memset(irq_handle_table, 0x00, sizeof(irq_handle_table));
    for (idx = 0; idx < MAX_INTR; idx ++)
    {
        irq_handle_table[idx].handler = (rt_isr_handler_t)rt_hw_interrupt_handler;
        irq_handle_table[idx].param = RT_NULL;
    }

    liointc_init();
}
