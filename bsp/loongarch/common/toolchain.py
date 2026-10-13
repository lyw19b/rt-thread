# SPDX-License-Identifier: Apache-2.0
import os

ARCH = 'loongarch'
CPU = 'loong64'  # Shared LP64D LWP port, including LA264.
CROSS_TOOL = os.getenv('RTT_CC', 'gcc')
if CROSS_TOOL != 'gcc':
    raise RuntimeError('The LS2K BSPs require the GNU LoongArch toolchain')

PLATFORM = 'gcc'
BUILD = 'debug'
RTT_ROOT = os.getenv('RTT_ROOT') or os.path.abspath('../../..')
EXEC_PATH = os.getenv('RTT_EXEC_PATH', '/usr/bin')
PREFIX = os.getenv('RTT_CC_PREFIX', 'loongarch64-linux-musl-')
MKIMAGE = os.getenv('MKIMAGE')

CC = PREFIX + 'gcc'
CXX = PREFIX + 'g++'
AS = CC
AR = PREFIX + 'ar'
LINK = CC
SIZE = PREFIX + 'size'
OBJDUMP = PREFIX + 'objdump'
OBJCPY = PREFIX + 'objcopy'
READELF = PREFIX + 'readelf'
TARGET_EXT = 'elf'

# LA264 has scalar floating point but requires aligned memory accesses.
DEVICE = ' -mcmodel=normal -march=loongarch64 -mabi=lp64d -mstrict-align '
CFLAGS = (DEVICE + ' -ffreestanding -Wno-cpp -fno-common -ffunction-sections'
          ' -fdata-sections -fstrict-volatile-bitfields -fdiagnostics-color=always'
          ' -O2 -ggdb -fvar-tracking -fno-pie -mno-lsx -mno-lasx ')
CXXFLAGS = CFLAGS
AFLAGS = ' -c' + DEVICE + ' -x assembler-with-cpp -D__ASSEMBLY__ -ggdb '
LFLAGS = (DEVICE + ' -nostartfiles -Wl,--gc-sections,-Map=rtthread.map,-cref,-u,_start'
          ' -T link.lds -lsupc++ -lgcc -static -no-pie ')
CPATH = ''
LPATH = ''


def image_actions(address, name):
    actions = OBJCPY + ' -O binary $TARGET rtthread.bin\n' + SIZE + ' $TARGET\n'
    if MKIMAGE:
        actions += (MKIMAGE + ' -A loongarch -O linux -T kernel -C none'
                    ' -a ' + address + ' -e ' + address + ' -n ' + name
                    + ' -d rtthread.bin uImage\n')
    return actions
