#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check synchronous exception routing against the port's actual C dispatcher."""

import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="host C compiler")
    args = parser.parse_args()
    source = (ROOT / "libcpu/loongarch/common/trap.c").read_text()
    start = source.index("void loongarch_dispatch_exception(")
    end = source.index("\n/**", start)
    dispatcher = source[start:end]
    harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "loongarch_regs.h"

typedef uint64_t rt_ubase_t;
struct pt_regs
{
    rt_ubase_t r_estat, r_bvaddr, guard;
};

static unsigned int selected, badv_reads;
static rt_ubase_t badv = UINT64_C(0x123456789abcdef0);

static rt_ubase_t csr_read64(unsigned int csr)
{
    assert(csr == LOONGARCH_CSR_BADV);
    badv_reads++;
    return badv;
}

static void record(struct pt_regs *regs, unsigned int handler)
{
    assert(regs->guard == UINT64_C(0xfedcba9876543210));
    assert(selected == 0);
    selected = handler;
}

#define HANDLER(name, number) \
    static void name(struct pt_regs *regs) { record(regs, number); }
HANDLER(do_ade, 1)
HANDLER(do_ale, 2)
HANDLER(do_bce, 3)
HANDLER(do_bp, 4)
HANDLER(do_fpe, 5)
HANDLER(do_fpu, 6)
HANDLER(do_ri, 7)

static void fatal_exception(struct pt_regs *regs, const char *reason)
{
    assert(strcmp(reason, "Unhandled synchronous exception") == 0);
    record(regs, 8);
}
"""
    harness += dispatcher
    harness += r"""
int main(void)
{
    /* Independent cause-to-handler oracle, including all reserved causes. */
    static const unsigned int expected[64] = {
        [8] = 1, [9] = 2, [10] = 3, [12] = 4,
        [13] = 7, [14] = 7, [15] = 6, [18] = 5
    };
    static const rt_ubase_t noise[] = {
        0, UINT64_C(0xffffffffffffffff), UINT64_C(0xa55aa55aa55aa55a)
    };
    unsigned int cause, pattern, checks = 0;

    for (cause = 0; cause < 64; cause++)
    {
        for (pattern = 0; pattern < sizeof(noise) / sizeof(noise[0]); pattern++)
        {
            struct pt_regs regs = {
                .r_estat = ((rt_ubase_t)cause << 16) | (noise[pattern] & ~UINT64_C(0x3f0000)),
                .r_bvaddr = UINT64_C(0x5a5a5a5a5a5a5a5a),
                .guard = UINT64_C(0xfedcba9876543210)
            };
            struct pt_regs before = regs;
            unsigned int address_fault = cause == 8 || cause == 9;

            selected = badv_reads = 0;
            loongarch_dispatch_exception(&regs);
            assert(selected == (expected[cause] ? expected[cause] : 8));
            assert(badv_reads == address_fault);
            assert(regs.r_estat == before.r_estat);
            assert(regs.r_bvaddr == (address_fault ? badv : before.r_bvaddr));
            assert(regs.guard == before.guard);
            checks++;
        }
    }
    printf("Synchronous exception dispatch: PASS (%u cases)\n", checks);
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="loongarch-exception-") as directory:
        directory = Path(directory)
        test = directory / "dispatch.c"
        executable = directory / "dispatch"
        test.write_text(harness)
        subprocess.run([
            args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "libcpu/loongarch/common/include"),
            str(test), "-o", str(executable)
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
