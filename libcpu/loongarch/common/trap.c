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
 * @brief    Provide the default no-op hook for unmasking a platform interrupt.
 *
 * @note     The BSP interrupt controller supplies the hardware implementation.
 *
 * @param    vector is the platform interrupt number.
 */
rt_weak void rt_hw_interrupt_umask(int vector)
{

}

/**
 * @brief    Install or query a handler in the fallback CPU interrupt table.
 *
 * @note     This weak fallback does not serialize concurrent updates. BSPs with a
 *           peripheral controller override it with their own registration interface.
 *
 * @param    vector is the CPU interrupt-table index.
 *
 * @param    handler is the new callback, or RT_NULL to query the current callback.
 *
 * @param    param is the argument passed to the new callback.
 *
 * @param    name is the name used by interrupt diagnostics.
 *
 * @return   The previous handler, or RT_NULL if vector is out of range.
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

/**
 * @brief    Provide the default no-op platform interrupt initialization hook.
 */
rt_weak void platform_irq_init()
{
}

/**
 * @brief    Dispatch pending CPU hardware interrupt bits through the fallback table.
 *
 * @note     Runs in interrupt context. BSPs with a cascaded interrupt controller
 *           override this hook to translate CPU input lines into peripheral sources.
 */
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


/**
 * @brief    Initialize the fallback interrupt table and invoke the platform hook.
 *
 * @note     Called during boot before device interrupts are enabled.
 */
rt_weak void rt_hw_interrupt_init()
{
    /* Populate the fallback table before platform initialization. */
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


/**
 * @brief    Terminate a faulting Smart process or stop on a kernel exception.
 *
 * @note     User-mode faults restore the interrupted IRQ state before process
 *           cleanup, which may sleep. Kernel faults do not return.
 *
 * @param    regs is the saved exception frame used to identify the faulting mode.
 *
 * @param    reason is the diagnostic description of the exception.
 */
static void fatal_exception(struct pt_regs *regs, const char *reason)
{
    rt_kprintf("%s: pc=%p address=%p\n", reason,
               (void *)regs->r_era, (void *)regs->r_bvaddr);
#ifdef RT_USING_SMART
    /* A user fault must not stop other processes or the kernel. */
    if (regs->r_prmd & 3)
    {
        rt_hw_local_irq_enable(regs->r_prmd & CSR_PRMD_PIE);
        sys_exit_group(-1);
    }
#endif
    rt_hw_interrupt_disable();
    while (1) {}
}

void do_ade(struct pt_regs *regs)
{
    fatal_exception(regs, "Address error");
}

void do_ale(struct pt_regs *regs)
{
    fatal_exception(regs, "Unaligned access");
}

void do_bce(struct pt_regs *regs)
{
    fatal_exception(regs, "Bounds check error");
}

void do_bp(struct pt_regs *regs)
{
    fatal_exception(regs, "Breakpoint");
}

void do_fpe(struct pt_regs *regs)
{
    fatal_exception(regs, "Floating-point exception");
}

void do_fpu(struct pt_regs *regs)
{
    fatal_exception(regs, "Floating-point unit disabled");
}

void do_ri(struct pt_regs *regs)
{
    fatal_exception(regs, "Illegal or privileged instruction");
}

/**
 * @brief Dispatch a synchronous exception using its saved hardware cause.
 * @param regs Complete exception frame supplied by the shared assembly entry.
 * @note Local IRQs remain masked until the selected handler decides how to
 *       terminate the faulting context. Only ADE/ALE populate the BADV snapshot;
 *       the other synchronous handlers retain the zero value from SAVE_ALL.
 */
void loongarch_dispatch_exception(struct pt_regs *regs)
{
    rt_ubase_t cause = (regs->r_estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT;

    switch (cause)
    {
    case EXCCODE_ADE:
        regs->r_bvaddr = csr_read64(LOONGARCH_CSR_BADV);
        do_ade(regs);
        break;
    case EXCCODE_ALE:
        regs->r_bvaddr = csr_read64(LOONGARCH_CSR_BADV);
        do_ale(regs);
        break;
    case EXCCODE_BCE:
        do_bce(regs);
        break;
    case EXCCODE_BP:
        do_bp(regs);
        break;
    case EXCCODE_FPE:
        do_fpe(regs);
        break;
    case EXCCODE_FPDIS:
        do_fpu(regs);
        break;
    case EXCCODE_INE:
    case EXCCODE_IPE:
        do_ri(regs);
        break;
    default:
        fatal_exception(regs, "Unhandled synchronous exception");
        break;
    }
}

/**
 * @brief    Dispatch one pending IPI, timer or platform interrupt class.
 *
 * @note     Called with local IRQs masked. Accounts interrupt nesting around
 *           each handler; the assembly return path performs any deferred scheduling.
 *
 * @param    regs is the saved exception frame supplied by the vector entry.
 */
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
#ifdef RT_USING_SMP
        rt_interrupt_enter();
        loongarch_ipi_handler();
        rt_interrupt_leave();
#endif
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

/**
 * @brief    Resolve a Smart mapping or permission fault in the appropriate address space.
 *
 * @note     Entered with local IRQs masked. The resolver runs with the interrupted
 *           IRQ state and may sleep; a repaired fault returns with IRQs masked again.
 *           Unresolved user faults exit the process; unresolved kernel faults stop.
 *
 * @param    regs is the saved frame containing BADV, ESTAT and the interrupted mode.
 */
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

    /* No address space can repair a kernel access to address zero. */
    if (!user && regs->r_bvaddr == 0)
        goto fatal;

    if (regs->r_bvaddr < USER_VADDR_TOP && lwp_self())
        aspace = lwp_self()->aspace;
    
    switch (code)
    {
    case MMU_STAT_EXCODE_PIL:
        msg.fault_op = MM_FAULT_OP_READ;
        break;
    case MMU_STAT_EXCODE_PME:
        msg.fault_type = MM_FAULT_TYPE_RWX_PERM;
        msg.fault_op = MM_FAULT_OP_WRITE;
        break;
    case MMU_STAT_EXCODE_PIS:
        msg.fault_op = MM_FAULT_OP_WRITE;
        break;
    case MMU_STAT_EXCODE_PIF:
        msg.fault_op = MM_FAULT_OP_EXECUTE;
        break;
    default:
        goto fatal;
    }
    
    /* Demand paging can sleep. Restore the interrupted context's IRQ state
     * while fixing the fault, then mask again before the exception return. */
    rt_hw_local_irq_enable(regs->r_prmd & CSR_PRMD_PIE);
    
    if (rt_aspace_fault_try_fix(aspace, &msg))
    {
        rt_hw_local_irq_disable();
        return;
    }

fatal:
    if (user)
    {
        rt_hw_local_irq_enable(regs->r_prmd & CSR_PRMD_PIE);
        rt_kprintf("User page fault: pc=%p address=%p code=%lu\n",
                   (void *)regs->r_era, (void *)regs->r_bvaddr, code);
        sys_exit_group(-1);
    }
#endif

    rt_kprintf("Kernel page fault: pc=%p address=%p ra=%p a0=%p a1=%p\n",
               (void *)regs->r_era, (void *)regs->r_bvaddr,
               (void *)regs->r_ra, (void *)regs->r_a0, (void *)regs->r_a1);
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

/**
 * @brief    Install the shared exception-vector addresses in the local CPU CSRs.
 *
 * @note     CPU 0 selects the shared vector addresses before secondary CPUs start.
 *           Software refill also installs TLBRENTRY on every CPU.
 */
static void configure_exception_vector(void)
{
    if (rt_hw_cpu_id() == 0)
    {
        eentry    = (unsigned long)exception_handlers;
        tlbrentry = (unsigned long)exception_handlers + 80*VECSIZE;
    }

    csr_write64(eentry, LOONGARCH_CSR_EENTRY);
    csr_write64(eentry, LOONGARCH_CSR_MERRENTRY);
#ifdef LOONGARCH_SOFTWARE_PTW
    csr_write64(tlbrentry, LOONGARCH_CSR_TLBRENTRY);
#endif
}

/**
 * @brief    Install a fixed vector stub and its handler address.
 *
 * @note     Only the boot CPU writes the shared vector image, before secondary
 *           CPUs can execute it. The barrier synchronizes local instruction fetch.
 *
 * @param    offset is the byte offset of the vector slot from EENTRY.
 *
 * @param    addr is the handler entry address stored in the stub.
 *
 * @param    size is the legacy argument; each installed stub occupies 40 bytes.
 */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
    extern char exception_vector_stub[];
    rt_memcpy((void *)(eentry + offset), exception_vector_stub, 40);
    *(rt_ubase_t *)(eentry + offset + 32) = (rt_ubase_t)addr;
    // clear ICache
    __asm__ volatile ("\tibar 0\n"::);
}

/**
 * @brief    Initialize the local vector CSRs and exception dispatch entries.
 *
 * @note     CPU 0 builds the shared 512-byte vector slots and software refill
 *           entry. Secondary CPUs reuse the published image and enable their IPI
 *           source. The caller keeps local IRQs masked throughout initialization.
 */
void trap_init(void)
{
    unsigned int i;

    setup_vint_size(7);

    configure_exception_vector();

    /* The boot CPU publishes the shared vector image before starting peers. */
    if (rt_hw_cpu_id() != 0)
    {
        __asm__ volatile("ibar 0" ::: "memory");
        set_csr_ecfg(ECFGF_IPI);
        return;
    }

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
