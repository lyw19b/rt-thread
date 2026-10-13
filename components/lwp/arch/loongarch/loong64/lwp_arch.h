/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */

#ifndef  LWP_ARCH_H__
#define  LWP_ARCH_H__

#include <rthw.h>
#include <lwp.h>
#include <lwp_arch_comm.h>

#ifdef ARCH_MM_MMU

#ifdef ARCH_MM_MMU_32BIT_LIMIT
#define USER_HEAP_VADDR     0xF0000000UL
#define USER_HEAP_VEND      0xFE000000UL
#define USER_STACK_VSTART   0xE0000000UL
#define USER_STACK_VEND     USER_HEAP_VADDR
#define USER_VADDR_START    0xC0000000UL
#define USER_VADDR_TOP      0xFF000000UL
#define USER_LOAD_VADDR     0xD0000000UL
#define LDSO_LOAD_VADDR     USER_LOAD_VADDR
#elif defined(ARCH_REMAP_KERNEL)
#define USER_VADDR_START    0x00001000UL
#define USER_VADDR_TOP      0x003ffffff000UL
#define USER_STACK_VSTART   0x000270000000UL
#define USER_STACK_VEND     (USER_HEAP_VADDR - (ARCH_PAGE_SIZE * 8)) /* start of ARGC ARGV ENVP. FIXME: space is ARG_MAX */
#define USER_HEAP_VADDR     0x000300000000UL
#define USER_HEAP_VEND      USER_VADDR_TOP
#define USER_LOAD_VADDR     0x200000000
#define LDSO_LOAD_VADDR     0x200000000
#else
#define USER_HEAP_VADDR     0x300000000UL
#define USER_HEAP_VEND      USER_STACK_VSTART
#define USER_STACK_VSTART   0x370000000UL
#define USER_STACK_VEND     0x400000000UL
#define USER_VADDR_START    0x200000000UL
#define USER_VADDR_TOP      0xfffffffffffff000UL
#define USER_LOAD_VADDR     0x200000000UL
#define LDSO_LOAD_VADDR     0x200000000UL
#endif

/* this attribution is cpu specified, and it should be defined in riscv_mmu.h */
#ifndef MMU_MAP_U_RWCB
#define MMU_MAP_U_RWCB 0
#endif

#ifndef MMU_MAP_U_RW
#define MMU_MAP_U_RW 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

rt_inline unsigned long rt_hw_ffz(unsigned long x)
{
    return __builtin_ffsl(~x) - 1;
}

// unsigned long rt_hw_ffz(unsigned long x);

rt_inline void icache_invalid_all(void)
{
    __asm__ volatile ("ibar 0":::"memory");
}

struct pt_regs;

/**
 * @brief Save signal-related context to user stack
 *
 * @param user_sp the current sp of user
 * @param exp_frame exception frame to resume former execution
 * @param psiginfo pointer to the siginfo
 * @param elr pc of former execution
 * @param spsr program status of former execution
 * @return void* the new user sp
 */
void *arch_signal_ucontext_save(rt_base_t user_sp, siginfo_t *psiginfo,
                                struct pt_regs *exp_frame,
                                lwp_sigset_t *save_sig_mask);

/**
 * @brief Restore the signal mask after return
 *
 * @param user_sp sp of user
 * @return void*
 */
void *arch_signal_ucontext_restore(rt_base_t user_sp, rt_base_t kernel_sp);
void arch_syscall_restart(void *sp, void *ksp);


#ifdef __cplusplus
}
#endif

#endif

#endif  /*LWP_ARCH_H__*/
