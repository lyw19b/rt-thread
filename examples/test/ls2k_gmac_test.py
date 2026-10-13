#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test LS2K GMAC logic, optionally cross-build or exercise a real UDP echo peer."""
import argparse
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BSP = ROOT / "bsp/loongarch/ls2kdev"
DRIVER = BSP / "drivers/net"


def run(command, **kwargs):
    subprocess.run([str(item) for item in command], check=True, **kwargs)


def host_tests(args, output):
    executable = output / "gmac-test"
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    run([args.cc, *flags, "-I", DRIVER, ROOT / "examples/test/ls2k_gmac_test.c",
         DRIVER / "gmac.c", "-o", executable])
    run([executable])

    # Compile the actual ethernetif function against a failing mailbox. Keep
    # this independent of the device model: it is a shared lwIP regression.
    source = (ROOT / "components/net/lwip/port/ethernetif.c").read_text()
    start = source.index("rt_err_t eth_device_ready(")
    end = source.index("\nrt_err_t eth_device_linkchange(", start)
    test = output / "rx-notice-test.c"
    test.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef int rt_err_t;
typedef int rt_base_t;
typedef uintptr_t rt_ubase_t;
#define RT_FALSE 0
#define RT_TRUE 1
#define RT_EOK 0
#define RT_ERROR 1
struct eth_device { void *netif; int rx_notice; int spinlock; };
static int eth_rx_thread_mb, locked, sends, fail;
static int rt_spin_lock_irqsave(int *lock)
{ (void)lock; assert(!locked); locked = 1; return 0; }
static void rt_spin_unlock_irqrestore(int *lock, int level)
{ (void)lock; (void)level; assert(locked); locked = 0; }
static int rt_mb_send(int *mailbox, uintptr_t value)
{ (void)mailbox; assert(value); assert(locked); sends++; return fail ? -1 : 0; }
''' + source[start:end] + r'''
int main(void)
{
    struct eth_device dev = {0};
    assert(eth_device_ready(&dev) < 0 && sends == 0 && !locked);
    dev.netif = &dev;
    fail = 1;
    assert(eth_device_ready(&dev) < 0 && !dev.rx_notice && sends == 1);
    fail = 0;
    assert(eth_device_ready(&dev) == 0 && dev.rx_notice && sends == 2);
    assert(eth_device_ready(&dev) == 0 && sends == 2);
    dev.rx_notice = 0; /* RX worker consumed the queued notification. */
    assert(eth_device_ready(&dev) == 0 && sends == 3 && !locked);
    puts("lwIP RX notification failure/retry/coalescing: PASS");
}
''')
    run([args.cc, *flags, test, "-o", output / "rx-notice-test"])
    run([output / "rx-notice-test"])


def set_config(config, name, value):
    config = re.sub(rf"^(?:CONFIG_{name}=.*|# CONFIG_{name} is not set)\n", "", config, flags=re.M)
    return config + f"CONFIG_{name}={value}\n"


def build_tests(args, output):
    env = os.environ.copy()
    if not env.get("RTT_EXEC_PATH"):
        candidates = sorted((Path.home() / "local").glob("*/bin/loongarch64-linux-musl-gcc"))
        if not candidates:
            raise RuntimeError("Set RTT_EXEC_PATH to the LoongArch compiler bin directory")
        env["RTT_EXEC_PATH"] = str(candidates[-1].parent)
    network = (BSP / "network_defconfig").read_text()
    variants = {
        "gmac0": {"BSP_USING_GMAC1": "n"},
        "gmac1": {"BSP_USING_GMAC0": "n"},
        "dual-small": {"BSP_GMAC_RING_SIZE": "4", "LWIP_NO_TX_THREAD": "y",
                       "RT_LWIP_ETH_PAD_SIZE": "2", "BSP_GMAC_USING_ECHO_TEST": "y"},
        "dual-large-ipv6": {"BSP_GMAC_RING_SIZE": "128", "RT_USING_LWIP212": "n",
                            "RT_USING_LWIP221": "y", "RT_USING_LWIP_IPV6": "y"},
        "standard": {},
    }
    original = (BSP / ".config").read_bytes()
    try:
        for name, options in variants.items():
            config = (BSP / "standard_defconfig").read_text() if name == "standard" else network
            for key, value in options.items():
                config = set_config(config, key, value)
            (BSP / ".config").write_text(config)
            log_path = output / f"{name}-build.log"
            with log_path.open("w") as log:
                run(["scons", "--pyconfig-silent"], cwd=BSP, env=env, stdout=log, stderr=subprocess.STDOUT)
                header = (BSP / "rtconfig.h").read_text()
                for key, value in options.items():
                    found = re.search(rf"^#define {key}(?: (.*))?$", header, re.M)
                    if value == "n":
                        assert not found, key
                    else:
                        assert found and (value == "y" or found.group(1) == value), key
                run(["scons", "-j8"], cwd=BSP, env=env, stdout=log, stderr=subprocess.STDOUT)
            if name != "standard":
                prefix = Path(env["RTT_EXEC_PATH"]) / env.get("RTT_CC_PREFIX", "loongarch64-linux-musl-")
                segments = subprocess.check_output([str(prefix) + "readelf", "-lW", BSP / "rtthread.elf"], text=True)
                assert ".gmac_dma" not in segments, "DMA section must not be zeroed by the ELF loader"
                symbols = subprocess.check_output([str(prefix) + "nm", "-n", BSP / "rtthread.elf"], text=True)
                addresses = {line.split()[-1]: int(line.split()[0], 16)
                             for line in symbols.splitlines() if len(line.split()) == 3}
                for symbol in ("dma0", "dma1"):
                    if symbol in addresses:
                        assert addresses["__bss_end"] <= addresses[symbol] < addresses["_end"]
                        assert addresses[symbol] & 63 == 0
                        assert addresses["_end"] & 0xffffffffffff <= 0x100000000
            # The driver must compile cleanly; retain the complete log on failure.
            if re.search(r"warning:|error:", log_path.read_text()):
                raise RuntimeError(f"Compiler diagnostics: {log_path}")
            print(f"{name}: build PASS", flush=True)
    finally:
        (BSP / ".config").write_bytes(original)
        with (output / "restore-config.log").open("w") as log:
            run(["scons", "--pyconfig-silent"], cwd=BSP, env=env, stdout=log, stderr=subprocess.STDOUT)


def payload(sequence):
    size = 16 + sequence % 1457
    return struct.pack("!Q", sequence) + bytes((sequence + i * 17) & 255 for i in range(size - 8))


def network_test(peer, frames, window):
    received = lost = duplicates = 0
    start = time.monotonic()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect((peer, 9000))
        sock.settimeout(0.2)
        for first in range(0, frames, window):
            pending = set(range(first, min(first + window, frames)))
            for sequence in sorted(pending):
                sock.send(payload(sequence))
            deadline = time.monotonic() + 2
            while pending and time.monotonic() < deadline:
                try:
                    data = sock.recv(2048)
                except socket.timeout:
                    continue
                if len(data) < 8:
                    raise RuntimeError("Truncated echo payload")
                sequence, = struct.unpack("!Q", data[:8])
                if sequence >= frames or data != payload(sequence):
                    raise RuntimeError(f"Corrupt echo payload: sequence {sequence}")
                if sequence in pending:
                    pending.remove(sequence)
                    received += 1
                else:
                    duplicates += 1
            lost += len(pending)
    print(f"{peer}: sent={frames} received={received} lost={lost} "
          f"late/duplicate={duplicates} seconds={time.monotonic() - start:.2f}")
    if lost or duplicates:
        raise RuntimeError("UDP stress did not complete without loss/duplicates")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build", action="store_true", help="also cross-build five BSP configurations")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--peer", action="append", default=[], help="board IPv4 address running gmac_echo")
    parser.add_argument("--frames", type=int, default=10000)
    parser.add_argument("--window", type=int, default=32)
    args = parser.parse_args()
    if args.frames < 1 or not 1 <= args.window <= 128:
        parser.error("frames must be positive and window must be 1..128")
    output = (args.output or Path(tempfile.mkdtemp(prefix="ls2k-gmac-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    print(f"Logs and executables: {output}", flush=True)
    host_tests(args, output)
    if args.build:
        build_tests(args, output)
    for peer in args.peer:
        network_test(peer, args.frames, args.window)


if __name__ == "__main__":
    main()
