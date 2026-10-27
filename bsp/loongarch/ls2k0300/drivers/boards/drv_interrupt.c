/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: drv interrupt.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <drv_interrupt.h>

#include <loongson_board_info.h>
#include <loongson_soc_info.h>
#include <loongarch.h>
#include <irq.h>

// 32
#define LIOINIT_CORE_IP_HWX_LOW_MAP 0x20
// 64
#define LIOINIT_CORE_IP_HWX_HIGH_MAP 0x40

// INT1 参考手册 表 3- 49 传统中断路由寄存器的说明
#define LIOINIT_LOW_ENTRY 0x2020202020202020
// INT2
#define LIOINIT_HIGH_ENTRY 0x4040404040404040

#define LIOINIT_LOW_IRQN  IS_CODE_HWI_1
#define LIOINIT_HIGH_IRQN IS_CODE_HWI_2

/*
 * 0-31 留给 loongarch 的架构中断控制器 但是这边认为不应该放在 libcpu 里面，不方便以后适配移植
 * 32 - 95 CPU自身的中断控制器
 * 96 - 201 GPIO中断注册 但是不用注册
 */
static struct rt_irq_desc irq_handle_table[SOC_LIO_IRQ_MAX];

static irq_desc_t irq_handlers[LIO_INT_IRQ_MAX_NUM];
static void ls2k_irq_dispatch_lioinit(int IRQn, void *param);
static void lio_setup_irq_pol_edge(int IRQn, uint8_t pol, uint8_t edge);
static irq_handler_t lio_irq_install_extend(int IRQn, irq_handler_t new_handler, void *param,
                                            uint8_t pol, uint8_t edge);
static void lio_irq_enable(int IRQn);
static void lio_irq_disable(int IRQn);
void ls2k_do_irq(int IRQn);
static uint8_t inline check_irq_num(int IRQn);

/*
 * 默认的中断处理函数
 * @IRQn 中断号
 * @param 参数
 */
static void irq_default_handler(int IRQn, void *param)
{
    printf("unhandled irq %d occured! (loongarch liointc)\r\n", IRQn);

    return;
}

/*
 * 设置中断处理函数
 * @IRQn 中断号
 * @new_handler 新设置的中断处理函数
 * @param 传递给中断处理函数的参数
 * @ret 之前的中断处理函数
 */
static irq_handler_t lio_irq_install(int IRQn, irq_handler_t new_handler, void *param)
{
    // 默认认为都是使用高电平触发
    return lio_irq_install_extend(IRQn, new_handler, param, 0, 0);
}

/*
 * 设置中断处理函数-拓展版
 * @IRQn 中断号
 * @new_handler 新设置的中断处理函数
 * @param 传递给中断处理函数的参数
 * @pol 对应传统中断的pol寄存器
 * @edge 对应传统中断的edge寄存器
 * @ret 之前的中断处理函数
 */
static irq_handler_t lio_irq_install_extend(int IRQn, irq_handler_t new_handler, void *param,
                                            uint8_t pol, uint8_t edge)
{
    irq_handler_t old_handler = 0;

    // printf("1 [%s] =======IRQn:%d\r\n", __FUNCTION__, IRQn);
    if ((0 <= IRQn) && (LIO_INT_IRQ_MAX_NUM > IRQn))
    {
        old_handler = irq_handlers[IRQn].handler;

        irq_handlers[IRQn].handler = new_handler;
        irq_handlers[IRQn].param = param;

        // printf("[%s] =======IRQn:%d\r\n", __FUNCTION__,IRQn);

        lio_setup_irq_pol_edge(IRQn, pol, edge);

        lio_irq_enable(IRQn);
    }

    return old_handler;
}

/*
 * 使能指定中断
 * @IRQn 中断号
 */
static void lio_irq_enable(int IRQn)
{
    unsigned int set_val, ien;

    if (check_irq_num(IRQn))
    {
        return;
    }

    if (IRQn < 32)
    {
        set_val = readl(INTIEN_0_REG);
        ien = (unsigned int)(1UL << IRQn);
        set_val |= ien;
        writel(set_val, INTSET_0_REG);
    }
    else
    {
        set_val = readl(INTIEN_1_REG);
        ien = (unsigned int)(1UL << (IRQn - 32));
        set_val |= ien;
        writel(set_val, INTSET_1_REG);
    }
}

/*
 * 禁止指定中断
 * @IRQn 中断号
 */
static void lio_irq_disable(int IRQn)
{
    unsigned int set_val, ien;

    if (check_irq_num(IRQn))
    {
        return;
    }

    if (IRQn < 32)
    {
        set_val = readl(INTIEN_0_REG);
        ien = (unsigned int)(1UL << IRQn);
        set_val |= ien;
        set_val ^= ien;
        writel(set_val, INTSET_0_REG);
    }
    else
    {
        set_val = readl(INTIEN_1_REG);
        ien = (unsigned int)(1UL << (IRQn - 32));
        set_val |= ien;
        set_val ^= ien;
        writel(set_val, INTSET_1_REG);
    }
}

// 初始化中断
static void lio_irq_init(void)
{
    int IRQn;

    writel(0x00000000, INTSET_0_REG);
    writel(0x00000000, INTSET_1_REG);

    writel(0xffffffff, INTCLR_0_REG);
    writel(0xffffffff, INTCLR_1_REG);

    // 设置默认的中断处理函数
    for (IRQn = 0; IRQn < LIO_INT_IRQ_MAX_NUM; IRQn++)
    {
        irq_handlers[IRQn].handler = irq_default_handler;
        irq_handlers[IRQn].param = 0;
    }

    // writel(0xffffffff, INTSET_0_REG);
    // writel(0xffffffff, INTSET_1_REG);

    writeq(LIOINIT_LOW_ENTRY, ENTRY0_0_REG);
    writeq(LIOINIT_LOW_ENTRY, ENTRY8_0_REG);
    writeq(LIOINIT_LOW_ENTRY, ENTRY16_0_REG);
    writeq(LIOINIT_LOW_ENTRY, ENTRY24_0_REG);

    writeq(LIOINIT_HIGH_ENTRY, ENTRY0_1_REG);
    writeq(LIOINIT_HIGH_ENTRY, ENTRY8_1_REG);
    writeq(LIOINIT_HIGH_ENTRY, ENTRY16_1_REG);
    writeq(LIOINIT_HIGH_ENTRY, ENTRY24_1_REG);

    loongarch_irq_install(LIOINIT_LOW_IRQN, ls2k_irq_dispatch_lioinit, 0);
    loongarch_irq_install(LIOINIT_HIGH_IRQN, ls2k_irq_dispatch_lioinit, 0);

    irq_handle_table[LIOINIT_LOW_IRQN].handler = (rt_isr_handler_t)ls2k_irq_dispatch_lioinit;
    irq_handle_table[LIOINIT_LOW_IRQN].param = RT_NULL;
    irq_handle_table[LIOINIT_HIGH_IRQN].handler = (rt_isr_handler_t)ls2k_irq_dispatch_lioinit;
    irq_handle_table[LIOINIT_HIGH_IRQN].param = RT_NULL;
}

static void ls2k_irq_dispatch_lioinit(int IRQn, void *param)
{
    unsigned int irq;
    unsigned int ien_val;
    unsigned int isr_val;
    unsigned int interrupt_clear_val;
    unsigned int *interrupt_clear_reg;
    unsigned int *interrupt_enable_reg;
    uint16_t offset;
    unsigned long intstatus;

    // rt_kprintf("IRQn: %d\r\n", IRQn);
    IRQn -= IS_CODE_HWI_0;
    IRQn = (1 << (4 + IRQn));
    offset = 0;

    if (IRQn == LIOINIT_CORE_IP_HWX_LOW_MAP)
    {
        ien_val = readl(INTIEN_0_REG);
        isr_val = readl(INTISR_0_REG);
        interrupt_enable_reg = (unsigned int *)INTSET_0_REG;
        interrupt_clear_reg = (unsigned int *)INTCLR_0_REG;
    }
    else if (IRQn == LIOINIT_CORE_IP_HWX_HIGH_MAP)
    {
        ien_val = readl(INTIEN_1_REG);
        isr_val = readl(INTISR_1_REG);
        offset = 32;
        interrupt_enable_reg = (unsigned int *)INTSET_1_REG;
        interrupt_clear_reg = (unsigned int *)INTCLR_1_REG;
    }
    else
    {
        return;
    }
    intstatus = ien_val & isr_val;
    // printf("1 intstatus:%x, entry val:%x\r\n", intstatus, IRQn);
    if (!intstatus)
    {
        return;
    }
    // printf("2 intstatus:%x, entry val:%x\r\n", intstatus, IRQn);
    irq = ffs(intstatus) - 1;
    // rt_kprintf("irq:%d, offset:%d\r\n", irq, offset);
    ls2k_do_irq(irq + offset);

    interrupt_clear_val = 0;
    interrupt_clear_val |= 1 << irq;
    writel(interrupt_clear_val, interrupt_clear_reg);
    writel(interrupt_clear_val, interrupt_enable_reg);
}

static void lio_setup_irq_pol_edge(int IRQn, uint8_t pol, uint8_t edge)
{
    unsigned char *irq_pol_reg;
    unsigned char *irq_edge_reg;
    unsigned int set_val, ien;
    uint8_t offset;

    if (IRQn < 32)
    {
        irq_pol_reg = (unsigned char *)INTPOL_0_REG;
        irq_edge_reg = (unsigned char *)INTEDGE_0_REG;
        offset = IRQn;
    }
    else
    {
        irq_pol_reg = (unsigned char *)INTPOL_1_REG;
        irq_edge_reg = (unsigned char *)INTEDGE_1_REG;
        offset = IRQn - 32;
    }

    set_val = readl(irq_edge_reg);
    ien = (unsigned int)(1UL << offset);
    set_val |= ien;
    if (!edge)
    {
        set_val ^= ien;
    }
    writel(set_val, irq_edge_reg);

    set_val = readl(irq_pol_reg);
    ien = (unsigned int)(1UL << offset);
    set_val |= ien;
    if (!pol)
    {
        set_val ^= ien;
    }
    writel(set_val, irq_pol_reg);
}

/*
 * 执行中断处理流程
 * @IRQn 中断号
 */
void ls2k_do_irq(int IRQn)
{
    irq_handler_t irq_handler = 0;
    void *param = 0;

    // printf("===>ls2k_do_irq:IRQn:%d\n",IRQn);
    irq_handler = irq_handlers[IRQn].handler;
    param = irq_handlers[IRQn].param;

    // 执行中断处理函数
    irq_handler(IRQn, param);

    return;
}

static uint8_t inline check_irq_num(int IRQn)
{
    return (IRQn < 0 || IRQn >= LIO_INT_IRQ_MAX_NUM) ? 1 : 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
/////////////////// rt thread kernel api ///////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// mask interrupt
void rt_hw_interrupt_mask(int irq)
{
    lio_irq_disable((irq - 32));
}

// unmask interrupt
void rt_hw_interrupt_umask(int irq)
{
    lio_irq_enable((irq - 32));
}

rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler, void *param,
                                         const char *name)
{
    rt_isr_handler_t old_handler = RT_NULL;

    if (vector >= 0 && vector < SOC_LIO_IRQ_MAX)
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

    // Loongarch 真正的中断管理
    if (vector < LOONGARCH_IRQ_MAX)
    {
        loongarch_irq_install(vector, handler, param);
    }
    else if (vector < SOC_LIO_IRQ_MAX)
    {
        lio_irq_install((vector - SOC_LIO_IRQ_START), handler, param);
    }
    else
    {
        // gpio 在 GPIO 驱动里面管理
        // LIO 需要打开对应中断
        // rt_kprintf("+++ lio irq %d\n", (vector - SOC_GPIO_IRQ_START));
        lio_irq_install((vector - SOC_GPIO_IRQ_START), handler, param);
    }
    return old_handler;
}

void rt_hw_interrupt_init()
{
    rt_uint32_t idx;

    rt_memset(irq_handle_table, 0x00, sizeof(irq_handle_table));
    for (idx = 0; idx < SOC_LIO_IRQ_MAX; idx++)
    {
        irq_handle_table[idx].handler = (rt_isr_handler_t)irq_default_handler;
        irq_handle_table[idx].param = RT_NULL;
    }

    lio_irq_init();
}
