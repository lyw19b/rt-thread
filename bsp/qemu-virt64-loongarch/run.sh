#!/bin/sh
set -eu
cd "$(dirname "$0")"
qemu=${QEMU:-qemu-system-loongarch64}
kernel=${KERNEL:-rtthread.elf}
cpus=$(awk '$1 == "#define" && $2 == "RT_CPUS_NR" {print $3}' rtconfig.h)
cpus=${cpus:-1}
cpu=${QEMU_CPU:-la464}
if grep -q '^#define LOONGARCH_HARDWARE_PTW$' rtconfig.h; then
    cpu=${QEMU_CPU:-la464,ptw=on}
fi

if grep -q '^#define RT_USING_NANO$' rtconfig.h; then
    exec "$qemu" -machine virt -cpu "$cpu" -smp "$cpus" -m 256M -nographic -kernel "$kernel" "$@"
fi

disk=${QEMU_DISK:-sd.img}
if [ ! -f "$disk" ]; then
    truncate -s 64M "$disk"
    mkfs.fat "$disk"
fi
exec "$qemu" -machine virt -cpu "$cpu" -smp "$cpus" -m 256M -nographic -kernel "$kernel" \
    -device "loader,file=$disk,addr=0xc000000,force-raw=on" "$@"
