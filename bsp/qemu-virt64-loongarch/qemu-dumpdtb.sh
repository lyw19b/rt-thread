#!/bin/sh
set -eu
"${QEMU:-qemu-system-loongarch64}" -machine "virt,dumpdtb=${1:-virt.dtb}" -m 256M -nographic
