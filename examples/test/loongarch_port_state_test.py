#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the actual port C code with page-allocation and CPU-state fault models."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

COMMON = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loongarch_regs.h"
typedef uintptr_t rt_ubase_t;
typedef intptr_t rt_base_t;
typedef int rt_bool_t;
struct rt_aspace { rt_ubase_t *page_table; };
typedef struct rt_aspace *rt_aspace_t;
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL
#define RT_CPUS_NR 2
#define ARCH_PAGE_SHIFT 12
#define ARCH_PAGE_SIZE 4096UL
#define ARCH_PAGE_MASK 4095UL
#define ARCH_PAGE_ADDRESS_MASK (~ARCH_PAGE_MASK)
/* Host pointers replace the target's DMW virtual/physical aliases. */
#define ARCH_PAGE_PA_MASK(p) ((uintptr_t)(p))
#define ARCH_PAGE_VA_MASK(p) (p)
#define rt_memset memset
"""

MMU_MODEL = r"""
#define PAGE_ANY_AVAILABLE 0
#define MMU_PAGE_PRESENT 128
#define MMU_PAGE_WRITE 256
#define MMU_PAGE_DIRTY 2
static unsigned locked, live, calls, fail_at, flushes, last_free;
static void *pages[16];
#define MM_PGTBL_LOCK(a) do { (void)(a); assert(!locked); locked = 1; } while (0)
#define MM_PGTBL_UNLOCK(a) do { (void)(a); assert(locked); locked = 0; } while (0)
static void *rt_pages_alloc_ext(int order, int flags)
{
    assert(locked && !order && !flags);
    if (++calls == fail_at)
        return NULL;
    for (unsigned i = 0; i < 16; i++)
        if (!pages[i])
        {
            pages[i] = aligned_alloc(4096, 4096);
            assert(pages[i]);
            live++;
            return pages[i];
        }
    abort();
}
static void loongarch_tlb_flush_all(void) { assert(locked); flushes++; }
static void rt_pages_free(void *page, int order)
{
    assert(locked && !order && flushes > last_free);
    last_free = flushes;
    for (unsigned i = 0; i < 16; i++)
        if (pages[i] == page)
        {
            free(page);
            pages[i] = NULL;
            live--;
            return;
        }
    abort();
}
"""

MMU_CHECKS = r"""
static uintptr_t translation(rt_aspace_t a, uintptr_t va)
{
    uintptr_t *pte = walk(a, va, RT_FALSE);
    return pte ? *pte : 0;
}
static void check_range(uintptr_t va, size_t size, unsigned allocations, int replace)
{
    const uintptr_t old_pa = 0x800000, new_pa = 0x1000000;
    const uintptr_t neighbor = 0x3fd00000;
    const uintptr_t attr = 1 | MMU_PAGE_WRITE;
    unsigned failure;

    /* Fail every allocation in turn, then exercise the successful operation. */
    for (failure = 1; failure <= allocations + 1; failure++)
    {
        uintptr_t root[512] = {0}, snapshot[512];
        struct rt_aspace a = {root};
        unsigned before;
        calls = fail_at = 0;
        assert(!live && !locked);
        if (replace)
        {
            assert(rt_hw_mmu_map(&a, (void *)va, (void *)old_pa, 4096, attr));
            assert(rt_hw_mmu_map(&a, (void *)neighbor, (void *)0x600000, 4096, attr));
        }
        before = live;
        memcpy(snapshot, root, sizeof(root));
        calls = 0;
        fail_at = failure;
        void *result = rt_hw_mmu_map(&a, (void *)va, (void *)new_pa, size, attr);
        assert(!locked);
        if (failure <= allocations)
        {
            assert(!result && calls == failure);
            assert(live == before);
            assert(!memcmp(snapshot, root, sizeof(root)));
            for (size_t offset = 0; offset < size; offset += 4096)
            {
                uintptr_t expected = replace && !offset ?
                    old_pa | attr | MMU_PAGE_PRESENT | MMU_PAGE_DIRTY : 0;
                assert(translation(&a, va + offset) == expected);
            }
        }
        else
        {
            assert(result == (void *)va && calls == allocations);
            for (size_t offset = 0; offset < size; offset += 4096)
                assert(translation(&a, va + offset) ==
                       ((new_pa + offset) | attr | MMU_PAGE_PRESENT | MMU_PAGE_DIRTY));
        }
        if (replace)
            assert(translation(&a, neighbor) ==
                   (0x600000 | attr | MMU_PAGE_PRESENT | MMU_PAGE_DIRTY));
        rt_hw_mmu_unmap(&a, (void *)va, size);
        if (replace)
            rt_hw_mmu_unmap(&a, (void *)neighbor, 4096);
        assert(!live && !locked);
        assert(table_empty(root));
    }
}
int main(void)
{
    check_range(0x1000, 4096, 2, 0);
    check_range(0x1ff000, 8192, 3, 0);
    check_range(0x3ffff000, 0x202000, 5, 0);
    check_range(0x1ff000, 8192, 1, 1);
    check_range(0x3ffff000, 0x202000, 3, 1);
    puts("MMU allocation rollback, replacement and reclamation: PASS (19 cases)");
}
"""

SWITCH_MODEL = r"""
static unsigned cpu, irq, invalidations, migration_attempts;
static uintptr_t pgdl[2];
static rt_base_t rt_hw_local_irq_disable(void)
{
    unsigned previous = irq;
    irq = 0;
    return previous;
}
static void rt_hw_local_irq_enable(rt_base_t state) { irq = state & CSR_CRMD_IE; }
static void migration_point(void)
{
    migration_attempts++;
    if (irq)
        cpu ^= 1;
}
static void csr_write64(uintptr_t pa, unsigned csr)
{
    assert(csr == LOONGARCH_CSR_PGDL);
    migration_point();
    pgdl[cpu] = pa;
}
static uintptr_t csr_read64(unsigned csr)
{
    assert(csr == LOONGARCH_CSR_PGDL);
    return pgdl[cpu];
}
static void rt_hw_tlb_invalidate_all_local(void)
{
    migration_point();
    assert(!irq);
    invalidations++;
}
"""

SWITCH_CHECKS = r"""
int main(void)
{
    struct rt_aspace target = {(void *)0xb000};
    assert(rt_hw_mmu_tbl_get() == NULL);
    for (unsigned initial = 0; initial <= CSR_CRMD_IE; initial += CSR_CRMD_IE)
    {
        irq = initial;
        cpu = 0;
        pgdl[0] = 0xa000;
        pgdl[1] = 0xc000;
        rt_hw_aspace_switch(&target);
        assert(cpu == 0 && irq == initial);
        assert(pgdl[0] == 0xb000 && pgdl[1] == 0xc000);
        assert(rt_hw_mmu_tbl_get() == target.page_table);
        cpu = 1;
        assert(rt_hw_mmu_tbl_get() == (void *)0xc000);
    }
    assert(invalidations == 2 && migration_attempts == 4);
    puts("Address-space switch exclusion, IRQ preservation and hardware root: PASS");
}
"""

SYSCALL_MODEL = r"""
struct pt_regs
{
    rt_ubase_t r_prmd, r_a0, r_a1, r_a2, r_a3, r_a4, r_a5, r_a6, r_a7;
    rt_ubase_t r_tp, r_sp, r_era;
};
static unsigned irq, adapter_irq, adapter_calls;
static void rt_hw_local_irq_enable(rt_base_t state) { irq = state & CSR_CRMD_IE; }
static void rt_hw_local_irq_disable(void) { irq = 0; }
rt_base_t loongarch_kernel_syscall(struct pt_regs *regs)
{
    assert(regs->r_a7 == 12345);
    adapter_irq = irq;
    adapter_calls++;
    return -ENOSYS;
}
#ifdef RT_USING_SMART
struct rt_thread { struct {void *ctx;} user_ctx; void *thread_idr; };
typedef struct rt_thread *rt_thread_t;
static struct rt_thread thread;
static rt_thread_t rt_thread_self(void) { return &thread; }
static void arch_signal_ucontext_restore(rt_ubase_t sp, rt_base_t regs)
{
    (void)sp; (void)regs;
    abort();
}
static rt_ubase_t user_call(rt_ubase_t a0, rt_ubase_t a1, rt_ubase_t a2,
                           rt_ubase_t a3, rt_ubase_t a4, rt_ubase_t a5, rt_ubase_t a6)
{
    assert(irq == CSR_CRMD_IE);
    assert(a0 == 10 && a1 == 11 && a2 == 12 && a3 == 13 &&
           a4 == 14 && a5 == 15 && a6 == 16);
    assert(((struct pt_regs *)thread.user_ctx.ctx)->r_era == 0x2000);
    thread.thread_idr = (void *)0x6000;
    return 42;
}
static void *lwp_get_sys_api(rt_ubase_t number)
{
    return number == 7 ? (void *)user_call : NULL;
}
#endif
"""

SYSCALL_CHECKS = r"""
int main(void)
{
    for (unsigned pie = 0; pie <= CSR_PRMD_PIE; pie += CSR_PRMD_PIE)
    {
        struct pt_regs regs = {.r_prmd = pie, .r_era = 0x1000, .r_a7 = 12345};
        irq = 0;
        rt_dispatch_syscall(&regs);
        assert(!irq && adapter_irq == pie);
        assert((rt_base_t)regs.r_a0 == -ENOSYS && regs.r_era == 0x1004);
        assert(regs.r_prmd == pie);
    }
    assert(adapter_calls == 2);
#ifdef RT_USING_SMART
    struct pt_regs user = {.r_prmd = 7, .r_era = 0x2000, .r_a7 = 7, .r_tp = 0x5000,
                          .r_a0 = 10, .r_a1 = 11, .r_a2 = 12, .r_a3 = 13,
                          .r_a4 = 14, .r_a5 = 15, .r_a6 = 16};
    rt_dispatch_syscall(&user);
    assert(!irq && user.r_a0 == 42 && user.r_era == 0x2004 && user.r_tp == 0x6000);
    user.r_a7 = 12345;
    rt_dispatch_syscall(&user);
    assert(!irq && (rt_base_t)user.r_a0 == -ENOSYS && user.r_era == 0x2008);
    assert(adapter_calls == 2);
#endif
    puts("Syscall IRQ state, return value and ERA: PASS");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    port = ROOT / "libcpu/loongarch/common"
    mmu = (port / "mmu.c").read_text()
    tables = mmu[mmu.index("static rt_ubase_t *table_from_entry"):
                 mmu.index("/**\n * @brief    Translate")]
    # The model checks C state transitions, not target memory-barrier semantics.
    tables = tables.replace('__asm__ volatile("dbar 0" ::: "memory");', "((void)0);")
    root_helper = mmu[mmu.index("static rt_ubase_t *table_from_entry"):mmu.index("\n/**")]
    switch = mmu[mmu.index("void rt_hw_aspace_switch("):
                 mmu.index("/**\n * @brief    Initialize")]
    syscall = (port / "syscall_c.c").read_text()
    syscall = syscall[syscall.index("#ifdef RT_USING_SMART"):]
    syscall = "\n".join(line for line in syscall.splitlines()
                        if not line.startswith("#include"))
    cases = {
        "mmu": COMMON + MMU_MODEL + tables + MMU_CHECKS,
        "switch": COMMON + SWITCH_MODEL + root_helper + switch + SWITCH_CHECKS,
        "syscall-smart": "#define RT_USING_SMART\n" + COMMON + SYSCALL_MODEL + syscall + SYSCALL_CHECKS,
        "syscall-dfs": "#define RT_USING_DFS\n" + COMMON + SYSCALL_MODEL + syscall + SYSCALL_CHECKS,
    }
    with tempfile.TemporaryDirectory(prefix="loongarch-state-") as directory:
        directory = Path(directory)
        for name, source in cases.items():
            test, executable = directory / f"{name}.c", directory / name
            test.write_text(source)
            subprocess.run([
                args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(port / "include"), str(test), "-o", str(executable)
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
