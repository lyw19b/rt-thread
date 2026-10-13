/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-13     LoongsonLab  fix bugs
 */

#include <rtthread.h>
// #include <rtthread.h>

#include "loongarch.h"

#define EIOINTC_REG_NODEMAP	0x14a0
#define EIOINTC_REG_IPMAP	0x14c0
#define EIOINTC_REG_ENABLE	0x1600
#define EIOINTC_REG_BOUNCE	0x1680
#define EIOINTC_REG_ISR		0x1800
#define EIOINTC_REG_ROUTE	0x1c00



static void eiointc_enable(void)
{
	uint64_t misc;

	misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_write64(misc, LOONGARCH_IOCSR_MISC_FUNC);
}


static void writeb(char value, volatile void *addr)
{
	*(volatile char *)addr = value;
}

static void writel(unsigned int value, volatile void *addr)
{
	*(volatile unsigned int *)addr = value;
}


void platform_irq_init() {

	int i, bit, cores, index, node;
	rt_uint32_t data;

	eiointc_enable();

	index = 0;

	for (i = 0; i < 256 / 32; i++) {
		data = (((1 << (i * 2 + 1)) << 16) | (1 << (i * 2)));
		iocsr_write32(data, EIOINTC_REG_NODEMAP + i * 4);
	}

	for (i = 0; i < 256 / 32 / 4; i++) {
		bit = BIT(1 + index); /* Route to IP[1 + index] */
		data = bit | (bit << 8) | (bit << 16) | (bit << 24);
		iocsr_write32(data, EIOINTC_REG_IPMAP + i * 4);
	}

	for (i = 0; i < 256 / 4; i++) {
		bit = 0;
		data = bit | (bit << 8) | (bit << 16) | (bit << 24);
		iocsr_write32(data, EIOINTC_REG_ROUTE + i * 4);
	}

	for (i = 0; i < 256 / 32; i++) {
		data = 0xffffffff;
		iocsr_write32(data, EIOINTC_REG_ENABLE + i * 4);
		iocsr_write32(data, EIOINTC_REG_BOUNCE + i * 4);
	}


#define PIC_COUNT_PER_REG	32
#define PIC_REG_COUNT		2
#define PIC_COUNT		(PIC_COUNT_PER_REG * PIC_REG_COUNT)
#define PIC_REG_IDX(irq_id)	((irq_id) / PIC_COUNT_PER_REG)
#define PIC_REG_BIT(irq_id)	((irq_id) % PIC_COUNT_PER_REG)
#define PIC_UNDEF_VECTOR	255

/* Registers */
#define PCH_PIC_MASK		0x20
#define PCH_PIC_HTMSI_EN	0x40
#define PCH_PIC_EDGE		0x60
#define PCH_PIC_CLR		0x80
#define PCH_PIC_AUTO0		0xc0
#define PCH_PIC_AUTO1		0xe0
#define PCH_INT_ROUTE(irq)	(0x100 + irq)
#define PCH_INT_HTVEC(irq)	(0x200 + irq)
#define PCH_PIC_POL		0x3e0

#define PHC_PIC_BASE ((void *)(CSR_DMW0_BASE | 0x10000000))


	for (i = 0; i < PIC_COUNT; i++) {
		/* Write vector ID */
		writeb(0 + i, PHC_PIC_BASE + PCH_INT_HTVEC(i));
		/* Hardcode route to HT0 Lo */
		writeb(1, PHC_PIC_BASE + PCH_INT_ROUTE(i));
	}

	for (i = 0; i < PIC_REG_COUNT; i++) {
		/* Clear IRQ cause registers, mask all interrupts */
		writel(0xFFFFFFFF, PHC_PIC_BASE + PCH_PIC_MASK + 4 * i);
		writel(0xFFFFFFFF, PHC_PIC_BASE + PCH_PIC_CLR + 4 * i);
		/* Clear auto bounce, we don't need that */
		writel(0, PHC_PIC_BASE + PCH_PIC_AUTO0 + 4 * i);
		writel(0, PHC_PIC_BASE + PCH_PIC_AUTO1 + 4 * i);
		/* Enable HTMSI transformer */
		writel(0xFFFFFFFF, PHC_PIC_BASE + PCH_PIC_HTMSI_EN + 4 * i);
	}


}


#define EIOINTC_MAX_HANDLERS 128
/* Exception and interrupt handler table */
struct rt_irq_desc eiointc_irq_desc[EIOINTC_MAX_HANDLERS];

void rt_hw_interrupt_umask(int vector)
{
	writel(0x0, PHC_PIC_BASE + PCH_PIC_MASK + 0);
}

rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
        void *param, const char *name)
{
	rt_isr_handler_t old_handler = RT_NULL;

	if (vector >= 0 && vector < EIOINTC_MAX_HANDLERS)
    {
        old_handler = eiointc_irq_desc[vector].handler;
        if (handler != RT_NULL)
        {
            eiointc_irq_desc[vector].handler = (rt_isr_handler_t)handler;
            eiointc_irq_desc[vector].param = param;
#ifdef RT_USING_INTERRUPT_INFO
            rt_snprintf(eiointc_irq_desc[vector].name, RT_NAME_MAX - 1, "%s", name);
            eiointc_irq_desc[vector].counter = 0;
#endif
        }
    }

    return old_handler;
}

#define VEC_COUNT_PER_REG 64

void platform_generic_irq()
{
	int i;
	unsigned long pending;
	for (i = 0; i < EIOINTC_MAX_HANDLERS / VEC_COUNT_PER_REG; i++) {
		pending = iocsr_read64(EIOINTC_REG_ISR + (i << 3));

		/* Skip handling if pending bitmap is zero */
		if (!pending)
			continue;

		/* Clear the IRQs */
		iocsr_write64(pending, EIOINTC_REG_ISR + (i << 3));
		while (pending) {
			int bit = __builtin_ctzl(pending);
			int irq = bit + VEC_COUNT_PER_REG * i;

            if (eiointc_irq_desc[irq].handler)
                eiointc_irq_desc[irq].handler(irq, eiointc_irq_desc[irq].param);
			pending &= ~(1UL << (bit));
		}
	}
}
