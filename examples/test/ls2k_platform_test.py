#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the shared LS2K interrupt/UART code with bounded mock MMIO."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="ls2k-platform-") as directory:
        output = Path(directory)
        (output / "drivers").mkdir()
        (output / "rtthread.h").write_text(r'''
#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int rt_err_t;
typedef uintptr_t rt_ubase_t;
typedef uintptr_t rt_base_t;
typedef size_t rt_size_t;
typedef uint8_t rt_uint8_t;
typedef uint32_t rt_uint32_t;
typedef uint64_t rt_uint64_t;
typedef void (*rt_isr_handler_t)(int, void *);
struct rt_irq_desc { rt_isr_handler_t handler; void *param; };
#define RT_NULL NULL
#define RT_EOK 0
#define RT_EINVAL 22
#define RT_ASSERT assert
#define RT_UNUSED(x) (void)(x)
#define rt_memset memset
#define RT_USING_UART0
#define RT_USING_UART4
#define BSP_UART_INDEX 0
#define BSP_UART_CLOCK_HZ 0
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
struct rt_spinlock { int unused; };
#define RT_SPINLOCK_INIT {0}
static inline void rt_spin_lock_init(struct rt_spinlock *lock) { lock->unused = 0; }
static inline rt_base_t rt_spin_lock_irqsave(struct rt_spinlock *lock)
{
    (void)lock;
    return rt_hw_interrupt_disable();
}
static inline void rt_spin_unlock_irqrestore(struct rt_spinlock *lock, rt_base_t level)
{
    (void)lock;
    rt_hw_interrupt_enable(level);
}
void rt_hw_dmb(void);
void rt_thread_mdelay(int ms);
void *test_mmio(uintptr_t address);
#define HWREG32(addr) (*(volatile uint32_t *)test_mmio(addr))
#define HWREG8(addr) (*(volatile uint8_t *)test_mmio(addr))
#endif
''')
        (output / "rthw.h").write_text('#include "rtthread.h"\n')
        (output / "cpuport.h").write_text("")
        (output / "loongarch.h").write_text("")
        (output / "drivers/dev_pin.h").write_text("""
#define PIN_IRQ_MODE_RISING 0
#define PIN_IRQ_MODE_FALLING 1
#define PIN_IRQ_MODE_HIGH_LEVEL 3
#define PIN_IRQ_MODE_LOW_LEVEL 4
""")
        for board in ("ls2k1000", "ls2k0300"):
            executable = output / board
            flags = ["-DTEST_LS2K1000"] if board == "ls2k1000" else []
            command = [os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                       "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer", *flags, "-I", str(output),
                       "-I", str(ROOT / "bsp/loongarch" / board / "drivers"),
                       "-I", str(ROOT / "libcpu/loongarch/ls2k"),
                       "-I", str(ROOT / "bsp/loongarch/common"),
                       str(ROOT / "examples/test/ls2k_platform_test.c"), "-o", str(executable)]
            subprocess.run(command, check=True)
            print(f"{board}:", flush=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
