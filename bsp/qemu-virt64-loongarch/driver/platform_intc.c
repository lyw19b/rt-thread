/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rthw.h>
#include "loongarch.h"

#define EIOINTC_NODEMAP      0x14a0
#define EIOINTC_IPMAP        0x14c0
#define EIOINTC_ENABLE      0x1600
#define EIOINTC_BOUNCE      0x1680
#define EIOINTC_ISR         0x1800
#define EIOINTC_ROUTE       0x1c00
#define EIOINTC_VECTORS      256

#define PIC_BASE            (CSR_DMW0_BASE | 0x10000000UL)
#define PIC_MASK            0x20
#define PIC_HTMSI           0x40
#define PIC_CLEAR           0x80
#define PIC_AUTO0           0xc0
#define PIC_AUTO1           0xe0
#define PIC_ROUTE           0x100
#define PIC_VECTOR          0x200
#define PIC_VECTORS         64

static struct rt_spinlock irq_lock;
static struct rt_irq_desc irq_table[EIOINTC_VECTORS];

static volatile rt_uint32_t *pic_reg(unsigned int offset)
{
    return (volatile rt_uint32_t *)(PIC_BASE + offset);
}

void platform_irq_init(void)
{
    unsigned int i;
    rt_uint64_t misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);

    rt_spin_lock_init(&irq_lock);
    iocsr_write64(misc | IOCSR_MISC_FUNC_EXT_IOI_EN, LOONGARCH_IOCSR_MISC_FUNC);
    for (i = 0; i < EIOINTC_VECTORS / 32; i++)
    {
        iocsr_write32((1U << (i * 2)) | (1U << (i * 2 + 17)), EIOINTC_NODEMAP + i * 4);
        iocsr_write32(0xffffffff, EIOINTC_ENABLE + i * 4);
        iocsr_write32(0xffffffff, EIOINTC_BOUNCE + i * 4);
    }
    /* Route all external vectors to core 0, CPU interrupt input IP1. */
    for (i = 0; i < EIOINTC_VECTORS / 128; i++)
        iocsr_write32(0x02020202, EIOINTC_IPMAP + i * 4);
    for (i = 0; i < EIOINTC_VECTORS / 4; i++)
        iocsr_write32(0, EIOINTC_ROUTE + i * 4);
    for (i = 0; i < PIC_VECTORS; i++)
    {
        *(volatile rt_uint8_t *)(PIC_BASE + PIC_VECTOR + i) = i;
        *(volatile rt_uint8_t *)(PIC_BASE + PIC_ROUTE + i) = 1;
    }
    for (i = 0; i < PIC_VECTORS / 32; i++)
    {
        *pic_reg(PIC_MASK + i * 4) = 0xffffffff;
        *pic_reg(PIC_CLEAR + i * 4) = 0xffffffff;
        *pic_reg(PIC_AUTO0 + i * 4) = 0;
        *pic_reg(PIC_AUTO1 + i * 4) = 0;
        *pic_reg(PIC_HTMSI + i * 4) = 0xffffffff;
    }
}

void rt_hw_interrupt_mask(int vector)
{
    if (vector >= 0 && vector < PIC_VECTORS)
    {
        rt_base_t level = rt_spin_lock_irqsave(&irq_lock);
        *pic_reg(PIC_MASK + vector / 32 * 4) |= 1U << (vector % 32);
        rt_spin_unlock_irqrestore(&irq_lock, level);
    }
}

void rt_hw_interrupt_umask(int vector)
{
    if (vector >= 0 && vector < PIC_VECTORS)
    {
        rt_base_t level = rt_spin_lock_irqsave(&irq_lock);
        *pic_reg(PIC_MASK + vector / 32 * 4) &= ~(1U << (vector % 32));
        rt_spin_unlock_irqrestore(&irq_lock, level);
    }
}

rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
                                        void *param, const char *name)
{
    rt_isr_handler_t previous = RT_NULL;

    if (vector >= 0 && vector < EIOINTC_VECTORS)
    {
        rt_base_t level = rt_spin_lock_irqsave(&irq_lock);
        previous = irq_table[vector].handler;
        irq_table[vector].handler = handler;
        irq_table[vector].param = param;
#ifdef RT_USING_INTERRUPT_INFO
        rt_strncpy(irq_table[vector].name, name ? name : "", RT_NAME_MAX - 1);
        irq_table[vector].counter = 0;
#endif
        rt_spin_unlock_irqrestore(&irq_lock, level);
    }
    return previous;
}

void platform_generic_irq(void)
{
    unsigned int i;

    for (i = 0; i < EIOINTC_VECTORS / 64; i++)
    {
        rt_uint64_t pending = iocsr_read64(EIOINTC_ISR + i * 8);
        iocsr_write64(pending, EIOINTC_ISR + i * 8);
        while (pending)
        {
            unsigned int bit = __builtin_ctzl(pending);
            unsigned int vector = i * 64 + bit;
            rt_base_t level = rt_spin_lock_irqsave(&irq_lock);
            rt_isr_handler_t handler = irq_table[vector].handler;
            void *parameter = irq_table[vector].param;
#ifdef RT_USING_INTERRUPT_INFO
            if (handler)
                irq_table[vector].counter++;
#endif
            rt_spin_unlock_irqrestore(&irq_lock, level);
            if (handler)
                handler(vector, parameter);
            pending &= pending - 1;
        }
    }
}
