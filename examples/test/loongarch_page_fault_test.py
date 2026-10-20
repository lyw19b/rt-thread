#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual LoongArch page-fault handler with VM/context stubs."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

MODEL = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "loongarch_regs.h"

typedef uintptr_t rt_ubase_t;
typedef int rt_bool_t;
struct pt_regs { rt_ubase_t r_estat, r_prmd, r_bvaddr, r_era, r_ra, r_a0, r_a1; };
struct rt_aspace { unsigned id; };
typedef struct rt_aspace *rt_aspace_t;
struct rt_aspace rt_kernel_space = {1};
static struct rt_aspace process_space = {2};
struct rt_lwp { rt_aspace_t aspace; };
static struct rt_lwp process;
#define USER_VADDR_START 0x1000UL
#define USER_VADDR_TOP 0x003ffffff000UL
#define MM_FAULT_TYPE_PAGE_FAULT 0
#define MM_FAULT_TYPE_RWX_PERM 1
#define MM_FAULT_OP_READ 0
#define MM_FAULT_OP_WRITE 1
#define MM_FAULT_OP_EXECUTE 2
#define MMU_STAT_EXCODE_PIL 1
#define MMU_STAT_EXCODE_PIS 2
#define MMU_STAT_EXCODE_PIF 3
#define MMU_STAT_EXCODE_PME 4
struct rt_aspace_fault_msg { void *fault_vaddr; int fault_type, fault_op; };

static jmp_buf stop;
static unsigned irq, critical, interrupt_nest, has_thread, has_process;
static unsigned resolver_calls, process_reads, repaired, reports;
static unsigned expected_irq, expected_space, expected_op, expected_type;
static struct pt_regs expected_frame;
static unsigned checks;

static int rt_scheduler_is_available(void)
{
    assert(!irq);
    return !critical && !interrupt_nest && has_thread;
}
static struct rt_lwp *lwp_self(void)
{
    process_reads++;
    assert(!critical && !interrupt_nest && has_thread);
    return has_process ? &process : NULL;
}
static void rt_hw_local_irq_enable(unsigned state) { irq = state & CSR_CRMD_IE; }
static void rt_hw_local_irq_disable(void) { irq = 0; }
static void __attribute__((unused)) rt_hw_interrupt_disable(void)
{
#ifdef RT_USING_SMP
    /* Fatal handling must not attempt to acquire a global SMP lock. */
    assert(0);
#else
    irq = 0;
#endif
}
static void test_halt(void)
{
    assert(!irq && reports == 1);
    longjmp(stop, 2);
}
static int rt_aspace_fault_try_fix(rt_aspace_t aspace, struct rt_aspace_fault_msg *msg)
{
    assert(!critical && !interrupt_nest && has_thread);
    assert(irq == expected_irq && aspace && aspace->id == expected_space);
    assert(msg->fault_vaddr == (void *)expected_frame.r_bvaddr);
    assert(msg->fault_op == (int)expected_op && msg->fault_type == (int)expected_type);
    resolver_calls++;
    return repaired;
}
static void rt_kprintf(const char *format, void *pc, void *address, ...)
{
    assert(pc == (void *)expected_frame.r_era && address == (void *)expected_frame.r_bvaddr);
    reports++;
    if (!strncmp(format, "Kernel page fault:", 18))
    {
        assert(!irq);
        return;
    }
    assert(!strncmp(format, "User page fault:", 16));
}
static void sys_exit_group(int status)
{
    assert(status == -1 && irq == expected_irq && reports == 1);
    longjmp(stop, 3);
}
"""

CHECKS = r"""
static void reset(unsigned mode, uintptr_t address, unsigned cause)
{
    irq = critical = interrupt_nest = resolver_calls = process_reads = reports = 0;
    has_thread = has_process = repaired = 1;
    process.aspace = &process_space;
    expected_irq = mode & CSR_PRMD_PIE;
    expected_space = address < USER_VADDR_TOP ? 2 : 1;
    expected_op = cause == 3 ? 2 : (cause == 2 || cause == 4 ? 1 : 0);
    expected_type = cause == 4 ? 1 : 0;
    expected_frame = (struct pt_regs){
        .r_estat = (uintptr_t)cause << CSR_ESTAT_EXC_SHIFT,
        .r_prmd = mode, .r_bvaddr = address, .r_era = 0x9000000000123450,
        .r_ra = 0x9000000000432100, .r_a0 = 0x1234, .r_a1 = 0x5678
    };
}
static void check(unsigned outcome, unsigned resolutions)
{
    struct pt_regs regs = expected_frame;
    unsigned result = setjmp(stop);
    if (!result)
        do_page_fault(&regs);
    assert(result == outcome);
    assert(!memcmp(&regs, &expected_frame, sizeof(regs)));
    assert(resolver_calls == resolutions);
    assert(reports == !!outcome);
    if (!outcome)
        assert(!irq);
    checks++;
}
int main(void)
{
#ifdef RT_USING_SMART
    static const uintptr_t low[] = {0, 0x10, USER_VADDR_START - 1};
    for (unsigned i = 0; i < sizeof(low) / sizeof(low[0]); i++)
    {
        reset(4, low[i], 1);
        check(2, 0);
        assert(!process_reads);
    }
    /* Every repairable access class: user, kernel/user copy, kernel mapping. */
    for (unsigned cause = 1; cause <= 4; cause++)
    {
        reset(7, USER_VADDR_START, cause); check(0, 1);
        reset(4, USER_VADDR_START, cause); check(0, 1);
        reset(4, USER_VADDR_TOP, cause); check(0, 1);
    }
    reset(4, USER_VADDR_START, 1);
    critical = 1; check(2, 0); assert(!process_reads);
    reset(4, USER_VADDR_START, 1);
    interrupt_nest = 1; check(2, 0); assert(!process_reads);
    reset(4, USER_VADDR_START, 1);
    has_thread = 0; check(2, 0); assert(!process_reads);
    reset(4, USER_VADDR_START, 1);
    process.aspace = NULL; check(2, 0);
    reset(4, USER_VADDR_TOP, 1);
    repaired = 0; check(2, 1);
    reset(7, USER_VADDR_START, 1);
    repaired = 0; check(3, 1);
    reset(7, 0, 1);
    repaired = 0; check(3, 1);
    reset(4, USER_VADDR_START, 63); check(2, 0);
    reset(7, USER_VADDR_START, 63); check(3, 0);
    reset(4, USER_VADDR_TOP, 1);
    has_process = 0; check(0, 1);
    reset(0, USER_VADDR_START, 1);
#ifdef RT_USING_SMP
    /* The SMP resolver permits IRQ-masked thread context without held locks. */
    check(0, 1);
#else
    check(2, 0);
#endif
#else
    reset(4, 0x10, 1); check(2, 0);
#endif
    printf("Page-fault context, routing, IRQ and original frame: PASS (%u cases)\n", checks);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--source", type=Path, default=ROOT / "libcpu/loongarch/common/trap.c")
    args = parser.parse_args()
    source = args.source.read_text()
    handler = source[source.index("void do_page_fault("):source.index("\n#define SZ_4K")]
    # Intercept the terminal halt after checking the complete fatal path.
    assert handler.count("while (1) {}") == 1
    handler = handler.replace("while (1) {}", "test_halt();")
    with tempfile.TemporaryDirectory(prefix="loongarch-page-fault-") as directory:
        directory = Path(directory)
        test = directory / "fault.c"
        test.write_text(MODEL + handler + CHECKS)
        for smart, smp in ((True, False), (True, True), (False, False), (False, True)):
            executable = directory / f"fault-{smart}-{smp}"
            flags = (["-DRT_USING_SMART"] if smart else []) + (["-DRT_USING_SMP"] if smp else [])
            subprocess.run([
                args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(ROOT / "libcpu/loongarch/common/include"),
                *flags, str(test), "-o", str(executable)
            ], check=True)
            print(f"Smart={smart}, SMP={smp}", flush=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
