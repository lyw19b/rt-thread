#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and exercise the LoongArch QEMU ports; keep logs outside the source tree."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

import pexpect

ROOT = Path(__file__).resolve().parents[2]
BSP = ROOT / "bsp/qemu-virt64-loongarch"
KERNEL_CASES = ("SMALL_MEM", "IRQ", "SEMAPHORE", "EVENT", "TIMER",
                "MESSAGEQUEUE", "MUTEX", "MAILBOX", "THREAD", "ATOMIC", "SCHEDULER")


def run(command, log, env=None, cwd=BSP):
    subprocess.run([str(item) for item in command], cwd=cwd, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True)


def build(mode, env, output):
    config = (BSP / f"{mode}_defconfig").read_text()
    if "CONFIG_BSP_USING_SELFTEST=y" not in config:
        config += "\nCONFIG_BSP_USING_SELFTEST=y\n"
    config += "CONFIG_RT_USING_UTEST=y\nCONFIG_RT_USING_UTESTCASES=y\n"
    config += "".join(f"CONFIG_RT_UTEST_{case}=y\n" for case in KERNEL_CASES)
    if mode == "smart":
        config += "CONFIG_RT_UTEST_MM_API=y\nCONFIG_RT_UTEST_MM_LWP=y\n"
    (BSP / ".config").write_text(config)
    with (output / f"{mode}-build.log").open("w") as log:
        run(["scons", "--pyconfig-silent"], log, env)
        run(["scons", "-j8"], log, env)
    kernel = output / f"{mode}.elf"
    shutil.copyfile(BSP / "rtthread.elf", kernel)
    return kernel


def disk_image(args, env, output):
    disk = output / "fat.img"
    with disk.open("wb") as file:
        file.truncate(64 * 1024 * 1024)
    with (output / "user-build.log").open("w") as log:
        run(["mkfs.fat", "-F", "32", disk], log)
        gcc = Path(env["RTT_EXEC_PATH"]) / (env.get("RTT_CC_PREFIX", "loongarch64-linux-musl-") + "gcc")
        run([gcc, "-nostdlib", "-static", "-no-pie", "-fno-pie", "-fno-builtin",
             "-mno-lsx", "-mno-lasx", "-O2", "-g", "-Wall", "-Wextra",
             "-Wl,-e,_start,-Ttext=0x200000000",
             ROOT / "examples/test/loongarch_user_test.c",
             ROOT / "examples/test/loongarch_user_entry.S", "-o", output / "user.elf"], log)
        run([args.mcopy, "-o", "-i", disk, output / "user.elf", "::user.elf"], log)
    return disk


class Guest:
    def __init__(self, args, kernel, disk, log):
        command = ["-M", "virt", "-m", "256M", "-nographic", "-kernel", str(kernel)]
        if disk:
            command += ["-device", f"loader,file={disk},addr=0xc000000,force-raw=on"]
        self.process = pexpect.spawn(args.qemu, command, encoding="utf-8", timeout=30)
        self.process.logfile_read = log
        try:
            self.expect(r"msh (?:/)?[>]")
        except Exception:
            self.close()
            raise

    def expect(self, pattern, timeout=30):
        result = self.process.expect([pattern, r"SMART FAIL:|KERNEL PORT TEST: FAIL|\[  FAILED  \]|Assertion failed|assertion failed|Kernel page fault"], timeout=timeout)
        if result:
            raise RuntimeError("Guest test failed; see QEMU log")
        return self.process.before

    def command(self, command, timeout=30):
        self.process.sendline(command)
        return self.expect(r"msh (?:/)?[>]", timeout)

    def kernel_test(self, iterations=200):
        output = self.command(f"port_test {iterations}", max(60, iterations / 100 + 30))
        if "KERNEL PORT TEST: PASS (0 failures)" not in output:
            raise RuntimeError("Missing kernel regression result")

    def user_test(self, command, marker, timeout=180):
        self.process.sendline(command)
        self.expect(marker, timeout)
        # Processes finish after printing their marker. Let the system thread reap
        # the last thread before collecting memory counters or reusing the shell.
        time.sleep(0.3)
        self.command("")

    def resources(self):
        text = self.command("port_resources")
        match = re.search(r"RESOURCES heap=(\d+) pages=(\d+)/(\d+)", text)
        if not match:
            raise RuntimeError("Missing resource counters")
        return tuple(map(int, match.groups()))

    def close(self):
        self.process.close(force=True)


def exercise(mode, args, kernel, disk, output):
    with (output / f"{mode}-qemu.log").open("w", buffering=1) as log:
        guest = Guest(args, kernel, disk if mode != "nano" else None, log)
        try:
            guest.kernel_test()
            guest.process.sendline("utest_run")
            results = guest.expect(r"\[ utest    \] finished", 900)
            expected = 32 if mode == "smart" else 19
            if not re.search(rf"\[  PASSED  \].*{expected} tests\.", results):
                raise RuntimeError(f"Expected {expected} passing official test cases")
            guest.command("")
            if mode == "smart":
                guest.user_test("/user.elf", "SMART REGRESSION PASS")
                baseline = guest.resources()
                for repeat in range(args.repeats):
                    guest.user_test(f"/user.elf stress {args.fork_loops}", "SMART STRESS PASS", 300)
                    current = guest.resources()
                    if current != baseline:
                        raise RuntimeError(f"Resource leak after round {repeat + 1}: {baseline} -> {current}")
                    print(f"smart: {args.fork_loops} fork/CoW cycles, resources {current}", flush=True)
            for repeat in range(args.repeats):
                guest.kernel_test(args.iterations)
                print(f"{mode}: kernel stress {repeat + 1}/{args.repeats}, {args.iterations} iterations/worker", flush=True)
            listing = guest.command("list thread")
            if "command not found" in listing:
                raise RuntimeError("Thread diagnostic command is unavailable")
        finally:
            guest.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("nano", "standard", "smart", "all"), default="all")
    parser.add_argument("--iterations", type=int, default=20000)
    parser.add_argument("--fork-loops", type=int, default=1024)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-loongarch64"))
    parser.add_argument("--mcopy", default=os.environ.get("MCOPY", "mcopy"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--parallel", action="store_true", help="run independent guests concurrently after building")
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000000 or not 1 <= args.fork_loops <= 100000 or args.repeats < 1:
        parser.error("test counts must be positive and within the guest limits")
    env = os.environ.copy()
    if "RTT_EXEC_PATH" not in env:
        candidates = sorted((Path.home() / "local").glob("*/bin/loongarch64-linux-musl-gcc"))
        if not candidates:
            parser.error("set RTT_EXEC_PATH to the cross compiler bin directory")
        env["RTT_EXEC_PATH"] = str(candidates[-1].parent)
    output = (args.output or Path(tempfile.mkdtemp(prefix="loongarch-qemu-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    print(f"Logs and images: {output}", flush=True)
    original = (BSP / ".config").read_bytes()
    try:
        modes = ("nano", "standard", "smart") if args.mode == "all" else (args.mode,)
        disk = disk_image(args, env, output) if any(mode != "nano" for mode in modes) else None
        kernels = {}
        for mode in modes:
            print(f"Building {mode}", flush=True)
            kernels[mode] = build(mode, env, output)
        if args.parallel:
            with ThreadPoolExecutor(max_workers=len(modes)) as pool:
                jobs = {mode: pool.submit(exercise, mode, args, kernels[mode], disk, output)
                        for mode in modes}
                for mode, job in jobs.items():
                    job.result()
                    print(f"{mode}: PASS", flush=True)
        else:
            for mode in modes:
                exercise(mode, args, kernels[mode], disk, output)
                print(f"{mode}: PASS", flush=True)
    finally:
        (BSP / ".config").write_bytes(original)
        with (output / "restore-config.log").open("w") as log:
            run(["scons", "--pyconfig-silent"], log, env)


if __name__ == "__main__":
    main()
