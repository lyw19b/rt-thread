#!/bin/sh
set -eu
cd "$(dirname "$0")"
qemu=${QEMU:-qemu-system-loongarch64}
kernel=${KERNEL:-rtthread.elf}

if grep -q '^#define RT_USING_NANO$' rtconfig.h; then
    exec "$qemu" -machine virt -m 256M -nographic -kernel "$kernel" "$@"
fi

disk=${QEMU_DISK:-sd.img}
if [ ! -f "$disk" ]; then
    truncate -s 64M "$disk"
    mkfs.fat "$disk"
fi
exec "$qemu" -machine virt -m 256M -nographic -kernel "$kernel" \
    -device "loader,file=$disk,addr=0xc000000,force-raw=on" "$@"
