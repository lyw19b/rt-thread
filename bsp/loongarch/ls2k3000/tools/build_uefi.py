#!/usr/bin/env python3
# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-28     LoongsonLab               align file header with LS2K0300

# Description:
#   Build a position-independent LoongArch64 firmware probe or kernel loader.
#   EDK II supplies ABI declarations only; no firmware library is linked.

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


BSP = Path(__file__).resolve().parents[1]
LINKER = '''ENTRY(efi_main)
OUTPUT_ARCH(loongarch)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    reloc PT_LOAD FLAGS(4);
}
SECTIONS
{
    . = 0x1000;
    .text : { *(.text .text.*) *(.rodata .rodata.*) } :code
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
    .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :data
    . = ALIGN(0x1000);
    .reloc : { LONG(0); LONG(12); SHORT(0); SHORT(0); } :reloc
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
'''


def validate_image(path):
    data = path.read_bytes()

    if data[:2] != b'MZ':
        raise ValueError('Missing DOS image signature')

    pe = struct.unpack_from('<I', data, 0x3c)[0]

    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Missing PE signature')

    machine, count, _, _, _, optional_size, flags = struct.unpack_from(
        '<HHIIIHH', data, pe + 4)
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    entry = struct.unpack_from('<I', data, optional + 16)[0]
    image_base = struct.unpack_from('<Q', data, optional + 24)[0]
    subsystem = struct.unpack_from('<H', data, optional + 68)[0]
    reloc_rva, reloc_bytes = struct.unpack_from('<II', data, optional + 112 + 5 * 8)

    if (machine != 0x6264 or magic != 0x20b or subsystem != 10 or
            image_base != 0 or flags & 1 or not reloc_rva or reloc_bytes < 12):
        raise ValueError('Invalid LoongArch64 relocatable EFI application headers')

    executable_entry = False
    reloc_valid = False

    for index in range(count):
        section = optional + optional_size + 40 * index
        virtual_bytes, rva, raw_bytes, raw = struct.unpack_from('<IIII', data, section + 8)
        characteristics = struct.unpack_from('<I', data, section + 36)[0]

        if raw_bytes and raw + raw_bytes > len(data):
            raise ValueError('Section exceeds file size')

        if rva <= entry < rva + virtual_bytes and characteristics & 0x20000000:
            executable_entry = True

        if rva <= reloc_rva and reloc_rva + reloc_bytes <= rva + raw_bytes:
            block = data[raw + reloc_rva - rva:raw + reloc_rva - rva + reloc_bytes]
            reloc_valid = block[:12] == struct.pack('<IIHH', 0, 12, 0, 0)

    if not executable_entry or not reloc_valid:
        raise ValueError('Missing executable entry or PIC relocation block')

    return {'machine': hex(machine), 'entry_rva': hex(entry), 'sections': count,
            'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edk2', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, help='Embed this LS2K3000 kernel ELF')
    args = parser.parse_args()
    output = args.output.resolve()
    rtt = BSP.parents[2]

    if output == rtt or rtt in output.parents:
        parser.error('Place generated images outside the RT-Thread source tree')

    include = args.edk2.resolve() / 'MdePkg/Include'

    if not (include / 'LoongArch64/ProcessorBind.h').is_file():
        parser.error('--edk2 must provide LoongArch64 MdePkg ABI headers')

    output.mkdir(parents=True, exist_ok=True)
    prefix = str(args.toolchain.resolve() / 'loongarch64-linux-musl-')
    sources = [BSP / ('boot/uefi_loader.c' if args.kernel else 'boot/uefi_probe.c')]
    kernel_hash = None

    if args.kernel:
        kernel = args.kernel.resolve()
        data = kernel.read_bytes()

        if (data[:6] != b'\x7fELF\x02\x01' or
                struct.unpack_from('<H', data, 18)[0] != 258 or
                struct.unpack_from('<Q', data, 24)[0] != 0x9000000002000000):
            parser.error('Kernel must be a little-endian LoongArch64 LS2K3000 ELF')

        symbols = subprocess.check_output([prefix + 'nm', '-n', str(kernel)], text=True)
        match = re.search(r'^([0-9a-f]+)\s+\w\s+_end$', symbols, re.MULTILINE)

        if not match or not 0x9000000002000000 < int(match[1], 16) < 0x9000000006000000:
            parser.error('Kernel BSS/DMA storage exceeds the reserved image arena')

        payload = output / 'payload.bin'
        subprocess.run([prefix + 'objcopy', '-O', 'binary', str(kernel), str(payload)], check=True)

        if not 0 < payload.stat().st_size <= 0x4000000:
            parser.error('Embedded kernel is empty or too large')

        kernel_hash = hashlib.sha256(payload.read_bytes()).hexdigest()
        payload_source = output / 'payload.S'
        payload_source.write_text('.section .rodata.payload,"a"\n.balign 4096\n'
                                  '.global ls2k3000_payload_start\nls2k3000_payload_start:\n'
                                  '.incbin ' + json.dumps(str(payload)) + '\n'
                                  '.global ls2k3000_payload_end\nls2k3000_payload_end:\n')
        sources += [BSP / 'boot/boot_contract.c', BSP / 'boot/uefi_memory.c',
                    BSP / 'boot/uefi_acpi.c',
                    BSP / 'boot/uefi_entry.S', payload_source]

    linker = output / 'probe.lds'
    elf = output / 'probe.elf'
    image = output / ('BOOTLOONGARCH64.EFI' if args.kernel else 'LS2K3000-PROBE.EFI')
    linker.write_text(LINKER.replace('    /DISCARD/',
                     '    .got : { *(.got*) }\n'
                     '    ASSERT(SIZEOF(.got) == 0, "EFI image must not require a GOT")\n'
                     '    /DISCARD/'))
    command = [prefix + 'gcc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-builtin', '-fno-tree-loop-distribute-patterns',
               '-fno-stack-protector',
               '-fpie', '-fvisibility=hidden', '-mabi=lp64d', '-march=loongarch64',
               '-mno-lsx', '-mno-lasx', '-mstrict-align', '-fshort-wchar',
               '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
               '-I' + str(include), '-I' + str(include / 'LoongArch64')]
    permitted = {'R_LARCH_PCALA_HI20', 'R_LARCH_PCALA_LO12', 'R_LARCH_B16',
                 'R_LARCH_B21', 'R_LARCH_B26', 'R_LARCH_RELAX', 'R_LARCH_ALIGN'}
    objects = []

    for source in sources:
        object_file = output / (source.name + '.o')
        subprocess.run(command + ['-c', str(source), '-o', str(object_file)], check=True)
        object_relocations = subprocess.check_output(
            [prefix + 'readelf', '-Wr', str(object_file)], text=True, env={'LC_ALL': 'C'})
        unexpected = set(re.findall(r'R_LARCH_\w+', object_relocations)) - permitted

        if unexpected:
            raise RuntimeError(str(source) + ': unsupported runtime fixups: ' + str(unexpected))

        objects.append(str(object_file))

    subprocess.run([prefix + 'gcc', '-nostdlib', '-static', '-no-pie',
                    '-Wl,--build-id=none', '-Wl,--no-relax', '-Wl,-T,' + str(linker),
                    *objects, '-o', str(elf)], check=True)
    relocations = subprocess.check_output([prefix + 'readelf', '-Wr', str(elf)],
                                         text=True, env={'LC_ALL': 'C'})

    if 'There are no relocations in this file.' not in relocations:
        raise RuntimeError('The PIC probe must not need runtime ELF relocations')

    subprocess.run([prefix + 'objcopy', '-O', 'pei-loongarch64',
                    '--subsystem=10', '--image-base=0', '--section-alignment=4096',
                    '--file-alignment=512', str(elf), str(image)], check=True)
    result = validate_image(image)
    result.update(status='BUILD_PASS_NOT_BOARD_TESTED',
                  sources={str(source): hashlib.sha256(source.read_bytes()).hexdigest()
                           for source in sources}, kernel_sha256=kernel_hash,
                  purpose='UEFI kernel handoff' if args.kernel else 'Read-only firmware inventory')
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
