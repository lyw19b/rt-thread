/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-22     LoongsonLab  fix tlb refill and page fault
 * 2025-03-23     LoongsonLab  add soft and hardware ptw
 */

#include "rtcompiler.h"
#include "rttypes.h"
#include <rtthread.h>
#include <rthw.h>
#ifdef RT_USING_SMART
#include <mmu.h>
#endif

#ifdef RT_USING_SMART
#include <lwp.h>
#include <lwp_arch.h>
#include <lwp_ipc.h>
#include <lwp_syscall.h>
#endif

#include "loongarch.h"
#include "stack.h"
#include "timer.h"
#ifdef RT_USING_SMART
#include "tlb.h"
#endif
#include "break.h"

#define MAX_HANDLERS 128

#define DEBUG_TRAP_TRACE 0

/* Exception and interrupt handler table */
struct rt_irq_desc irq_desc[MAX_HANDLERS];

// default function
static void rt_hw_interrupt_default_handle(int vector, void *param)
{
    rt_kprintf("UN-handled interrupt %d occurred!!!\n", vector);
}


extern void handle_vint(void);

extern void handle_ade(void);
extern void handle_ale(void);
extern void handle_bce(void);
extern void handle_bp(void);
extern void handle_fpe(void);
extern void handle_fpu(void);
extern void handle_ri(void);

extern void handle_syscall(void);

extern void handle_tlb_load_ptw(void);
extern void handle_tlb_store_ptw(void);
extern void handle_tlb_modify_ptw(void);

extern void handle_tlb_sw_refill(void);

/**
 * This function will un-mask a interrupt.
 * @param vector the interrupt number
 */
rt_weak void rt_hw_interrupt_umask(int vector)
{

}

/**
 * This function will install a interrupt service routine to a interrupt.
 * @param vector the interrupt number
 * @param new_handler the interrupt service routine to be installed
 * @param old_handler the old interrupt service routine
 */
rt_weak rt_isr_handler_t rt_hw_interrupt_install(int vector, rt_isr_handler_t handler,
        void *param, const char *name)
{
    rt_isr_handler_t old_handler = RT_NULL;

    if (vector >= 0 && vector < MAX_HANDLERS)
    {
        old_handler = irq_desc[vector].handler;
        if (handler != RT_NULL)
        {
            irq_desc[vector].handler = (rt_isr_handler_t)handler;
            irq_desc[vector].param = param;
#ifdef RT_USING_INTERRUPT_INFO
            rt_snprintf(irq_desc[vector].name, RT_NAME_MAX - 1, "%s", name);
            irq_desc[vector].counter = 0;
#endif
        }
    }

    return old_handler;
}

rt_weak void platform_irq_init()
{
}

rt_weak void platform_generic_irq()
{
    rt_uint64_t hw_irq_pending = (read_csr_estat() & CSR_ESTAT_IS_HW);
    rt_uint64_t hw_irq_index = 0;

    while (hw_irq_pending) {
        rt_uint64_t bit = __builtin_ffsll(hw_irq_pending);
        hw_irq_index = bit - 1 ;

        irq_desc[hw_irq_index].handler(hw_irq_index, irq_desc[hw_irq_index].param);
        hw_irq_pending &= ~(1UL << ( bit - 1));

#ifdef RT_USING_INTERRUPT_INFO0
        rt_snprintf(irq_desc[hw_irq_index].name, RT_NAME_MAX - 1, "%s", name);
        irq_desc[hw_irq_index].counter ++;
#endif

#if DEBUG_TRAP_TRACE
// #if 1
        rt_kprintf("     Hardware Exception ID: %d\n", hw_irq_index);
#endif
    }
}


rt_weak void rt_hw_interrupt_init()
{
    /* Enable machine external interrupts. */
    int idx = 0;
    /* init exceptions table */
    for (idx = 0; idx < MAX_HANDLERS; idx++)
    {
        irq_desc[idx].handler = (rt_isr_handler_t)rt_hw_interrupt_default_handle;
        irq_desc[idx].param = RT_NULL;
#ifdef RT_USING_INTERRUPT_INFO
        rt_snprintf(irq_desc[idx].name, RT_NAME_MAX - 1, "default");
        irq_desc[idx].counter = 0;
#endif
    }

    platform_irq_init();
}

// default function
static void rt_hw_exception_default_handle(void)
{
    rt_kprintf("UN-handled reserved exception occurred!!!\n");
}

void *exception_table[EXCCODE_INT_START] = {
    [0 ... EXCCODE_INT_START - 1] = rt_hw_exception_default_handle,

#ifdef RT_USING_SMART
    [EXCCODE_TLBI]       = handle_tlb_load_ptw,
    [EXCCODE_TLBL]       = handle_tlb_load_ptw,
    [EXCCODE_TLBS]       = handle_tlb_store_ptw,
    [EXCCODE_TLBM]       = handle_tlb_modify_ptw,
    [EXCCODE_TLBNR]      = handle_tlb_load_ptw,
    [EXCCODE_TLBNX]      = handle_tlb_load_ptw,
    [EXCCODE_TLBPE]      = handle_tlb_load_ptw,
#endif

    [EXCCODE_ADE]        = handle_ade,
    [EXCCODE_ALE]        = handle_ale,
    [EXCCODE_BCE]        = handle_bce,
    [EXCCODE_SYS]        = handle_syscall,
    [EXCCODE_BP]         = handle_bp,
    [EXCCODE_INE]        = handle_ri,
    [EXCCODE_IPE]        = handle_ri,
    [EXCCODE_FPDIS]      = handle_fpu,
    [EXCCODE_FPE]        = handle_fpe,
};


void do_ade(struct pt_regs *regs)
{
    rt_kprintf("UN-handled do_ade exception occurred!!!\n");
    rt_kprintf("Ra :\n");
    rt_kprintf("    0x%lx\n", regs->r_ra);
    rt_kprintf("Era:\n");
    rt_kprintf("    0x%lx\n", regs->r_era);
    rt_kprintf("EsubCode:\n");
    rt_kprintf("    0x%lx\n", ((read_csr_estat() & CSR_ESTAT_ESUBCODE) >> CSR_ESTAT_ESUBCODE_SHIFT));
    rt_kprintf("bvaddr: 0x%lx\n", regs->r_bvaddr);
    while(1);
}

void do_ale(struct pt_regs *regs)
{
    rt_kprintf("UN-handled do_ale exception occurred!!!\n");
    rt_kprintf("ra: 0x%lx\n", regs->r_ra);
    rt_kprintf("era: 0x%lx\n", regs->r_era);
    rt_kprintf("bvaddr: 0x%lx\n", regs->r_bvaddr);
    while(1);
    // emulate_load_store_insn(regs, regs->r_bvaddr, regs->r_era);
}

void do_bce(struct pt_regs *regs)
{
    rt_kprintf("UN-handled do_bce exception occurred!!!\n");
    rt_kprintf("Ra :\n");
    rt_kprintf("    0x%lx\n", regs->r_ra);
    rt_kprintf("Era:\n");
    rt_kprintf("    0x%lx\n", regs->r_era);
    while(1);
}

void do_bp(struct pt_regs *regs)
{
    rt_uint64_t era = regs->r_era;
    rt_uint32_t opcode = *(rt_uint32_t *)(era);
    rt_uint32_t bcode = (opcode & 0x7fff);

    rt_kprintf("era: %016lx, opcode: %08x\n", era, opcode);

    switch (bcode)
    {
    case BRK_DIVZERO:
        rt_kprintf("BRK_DIVZERO\n");
        break;
    default:
        rt_kprintf("UN-handled do_bp exception occurred!!!\n");
        break;
    }

    while(1);
}

void do_fpe(struct pt_regs *regs)
{
    rt_kprintf("UN-handled do_fpe exception occurred!!!\n");
    while(1);
}

void do_fpu(struct pt_regs *regs)
{
    rt_kprintf("UN-handled do_fpu exception occurred!!!\n");
    while(1);
}

void do_ri(struct pt_regs *regs)
{
    rt_uint64_t era = regs->r_era;
    rt_uint32_t opcode = *(rt_uint32_t *)(era);
    rt_uint32_t bcode = (opcode & 0x7fff);
    rt_kprintf("era: %016lx, opcode: %08x\n", era, opcode);

    rt_kprintf("UN-handled do_ri exception occurred!!!\n");
    rt_kprintf(" Ra:\n");
    rt_kprintf("    0x%lx\n", regs->r_ra);
    rt_kprintf("Era:\n");
    rt_kprintf("    0x%lx\n", regs->r_era);
    while (1);
}

void do_rt_dispatch_trap(struct pt_regs *regs)
{
    rt_uint64_t estat;
    rt_thread_t thread = rt_thread_self();

    estat = read_csr_estat() & CSR_ESTAT_IS;
#if DEBUG_TRAP_TRACE
    rt_kprintf("Exception Thread: 0x%lx, pt_regs: 0x%lx\n", thread, regs);
#endif
    if ((estat & CSR_ESTAT_IS_IPI))
    {
#if DEBUG_TRAP_TRACE
        rt_kprintf("---------- Enter Interrupt ----------\n");
        rt_kprintf("      IPI Exception occurred!        \n");
#endif
        while (1);
    } else if ((estat & CSR_ESTAT_IS_TI))
    {
#if DEBUG_TRAP_TRACE
        rt_kprintf("---------- Enter Interrupt ----------\n");
        rt_kprintf("     Timer Exception occurred!       \n");
#endif
        rt_interrupt_enter();
        write_csr_tintclear(CSR_TINTCLR_TI);
        rt_hw_timer_handler();
        rt_interrupt_leave();
        return;
    } else if ((estat & CSR_ESTAT_IS_HW))
    {
#if DEBUG_TRAP_TRACE
        rt_kprintf("---------- Enter Interrupt ----------\n");
        rt_kprintf("     Hardware Exception occurred: 0x%lx\n", estat);
#endif
        // fixme
        // CSR_ESTAT_IS_HW is read-only
        csr_xchg32(0x0, CSR_ESTAT_IS_HW, LOONGARCH_CSR_ESTAT);
        rt_interrupt_enter();
        platform_generic_irq();
        rt_interrupt_leave();
        return;
    } else {
#if DEBUG_TRAP_TRACE
        rt_kprintf("---------- Enter Interrupt ----------\n");
        rt_kprintf("UN-handled rt_dispatch_trap exception occurred!!!\n");
#endif
        while(1);
    }
}

void do_page_fault(struct pt_regs *regs)
{
#ifdef RT_USING_SMART
    rt_ubase_t code = (regs->r_estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT;
    rt_bool_t user = (regs->r_prmd & 3) != 0;
    struct rt_aspace_fault_msg msg = {
        .fault_vaddr = (void *)regs->r_bvaddr,
        .fault_type = MM_FAULT_TYPE_PAGE_FAULT,
    };
    rt_aspace_t aspace = &rt_kernel_space;

    if (regs->r_bvaddr < USER_VADDR_TOP && lwp_self())
        aspace = lwp_self()->aspace;
    switch (code)
    {
    case MMU_STAT_EXCODE_PIL:
        msg.fault_op = MM_FAULT_OP_READ;
        break;
    case MMU_STAT_EXCODE_PIS:
    case MMU_STAT_EXCODE_PME:
        msg.fault_op = MM_FAULT_OP_WRITE;
        break;
    case MMU_STAT_EXCODE_PIF:
        msg.fault_op = MM_FAULT_OP_EXECUTE;
        break;
    default:
        goto fatal;
    }
    if (rt_aspace_fault_try_fix(aspace, &msg))
        return;
fatal:
    if (user)
    {
        rt_kprintf("User page fault: pc=%p address=%p code=%lu\n",
                   (void *)regs->r_era, (void *)regs->r_bvaddr, code);
        sys_exit_group(-1);
    }
#endif
    rt_kprintf("Kernel page fault: pc=%p address=%p\n",
               (void *)regs->r_era, (void *)regs->r_bvaddr);
    rt_hw_interrupt_disable();
    while (1) {}
}

#define SZ_4K       0x00001000
#define SZ_8K       0x00002000
#define SZ_16K      0x00004000
#define SZ_32K      0x00008000
#define SZ_64K      0x00010000
#define VECSIZE     0x200

rt_align(SZ_64K) long exception_handlers[VECSIZE * 128 / sizeof(long)];

unsigned long eentry;
unsigned long tlbrentry;

static void setup_vint_size(unsigned int size)
{
    unsigned int vs;
    //TODO:
    vs = size;
    csr_xchg32(vs<<CSR_ECFG_VS_SHIFT, CSR_ECFG_VS, LOONGARCH_CSR_ECFG);
}

static void configure_exception_vector(void)
{
    eentry    = (unsigned long)exception_handlers;
    tlbrentry = (unsigned long)exception_handlers + 80*VECSIZE;

    csr_write64(eentry, LOONGARCH_CSR_EENTRY);
    csr_write64(eentry, LOONGARCH_CSR_MERRENTRY);
#ifdef LOONGARCH_SOFTWARE_PTW
    csr_write64(tlbrentry, LOONGARCH_CSR_TLBRENTRY);
#endif
}

/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
    extern char exception_vector_stub[];
    rt_memcpy((void *)(eentry + offset), exception_vector_stub, 40);
    *(rt_ubase_t *)(eentry + offset + 32) = (rt_ubase_t)addr;
    // clear ICache
    __asm__ volatile ("\tibar 0\n"::);
}

void trap_init(void)
{
    unsigned int i;

    setup_vint_size(7);

    configure_exception_vector();

    // copy exception handler to exception vector

    /* Set interrupt vector handler */
    for (i = EXCCODE_INT_START; i <= EXCCODE_INT_END; i++)
        set_handler(i * VECSIZE, handle_vint, VECSIZE);

    /* Set exception vector handler */
    for (i = EXCCODE_ADE; i <= EXCCODE_BTDIS; i++)
        set_handler(i * VECSIZE, exception_table[i], VECSIZE);

    for (int i = EXCCODE_TLBL; i <= EXCCODE_TLBPE; i++)
        set_handler(i * VECSIZE, exception_table[i], VECSIZE);

#ifdef LOONGARCH_SOFTWARE_PTW
    rt_memcpy((void *)tlbrentry, handle_tlb_sw_refill, 0x80);
    __asm__ volatile ("\tibar 0\n"::);
#endif

    set_csr_ecfg(ECFGF_SIP0 | ECFGF_SIP1 | ECFGF_IP0 | ECFGF_IP1 | ECFGF_IP2 | ECFGF_IP3 | ECFGF_IP4 | ECFGF_IP5 | ECFGF_IP6 | ECFGF_IP7 | ECFGF_IPI | ECFGF_PMC);

}
