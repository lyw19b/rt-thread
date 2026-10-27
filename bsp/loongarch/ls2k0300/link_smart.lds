/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020/12/12     bernard      The first version
 * 2025/3/10      LoongsonLab  modify arch and address
 * 2026/08/16     smart        smart-mode layout: kernel in H-DDR
 *
 * Smart-mode linker script. The kernel image is loaded into the 2K0300 H-DDR
 * region (physical 0x98000000, 2GB window at 0x80000000-0xffffffff) via U-Boot
 * TFTP/JTAG, accessed through the cached DMW1 window
 * (CACHED_MEMORY_ADDR = 0x9000000000000000).
 *
 * The entry/link address 0x9000000098000000 intentionally matches the NoMMU
 * layout (link.lds) so U-Boot loading and the startup path stay identical
 * between smart and NoMMU builds. The 16MB LENGTH covers the smart+LWP image
 * (~750KB) loaded via U-Boot directly into H-DDR.
 *
 * Memory layout (all via cached DMW1 window, physical = virtual & 0xffffffffffff):
 *   physical 0x98000000 - 0x99000000  (16MB H-DDR)
 *   link/run 0x9000000098000000 - 0x9000000099000000
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: link smart.
 */

INCLUDE "link_stacksize.lds"

OUTPUT_FORMAT("elf64-loongarch", "elf64-loongarch", "elf64-loongarch")
OUTPUT_ARCH(loongarch)

MEMORY
{
   SRAM : ORIGIN = 0x9000000098000000, LENGTH = 0x1000000
}

ENTRY(_start)
SECTIONS
{
    . = 0x9000000098000000;

    .start :
    {
        __image_copy_start = .;
        __text_start = .;
        *(.start)
    } > SRAM

    . = ALIGN(8);

    .text :
    {
        *(.text)                        /* remaining code */
        *(.text.*)                      /* remaining code */
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)

        /* section information for finsh shell */
        . = ALIGN(8);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(8);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(8);

        /* section information for initial. */
        . = ALIGN(8);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        . = ALIGN(8);

        __rt_utest_tc_tab_start = .;
        KEEP(*(UtestTcTab))
        __rt_utest_tc_tab_end = .;

        . = ALIGN(8);
        _etext = .;
    } > SRAM

    .eh_frame_hdr :
    {
         *(.eh_frame_hdr)
         *(.eh_frame_entry)
    } > SRAM
    .eh_frame : ONLY_IF_RO { KEEP (*(.eh_frame)) } > SRAM

    . = ALIGN(8);
    __text_end = .;
    __text_size = __text_end - __text_start;

    . = ALIGN(4);
    .rodata :
    {
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
    } > SRAM

    . = ALIGN(4);
    .data :
    {
        *(.data)
        *(.data.*)

        *(.data1)
        *(.data1.*)

        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );

        *(.sdata)
        *(.sdata.*)
    } > SRAM

    . = ALIGN(8);
    .ctors :
    {
        PROVIDE(__ctors_start__ = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__ctors_end__ = .);
    } > SRAM

    .dtors :
    {
        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__dtors_end__ = .);
    } > SRAM


    /* stack for dual core */
    .stack :
    {
        . = ALIGN(64);
        __stack_start__ = .;

        . += __STACKSIZE__;

        . = ALIGN(8);
        __stack_end__ = .;
    } > SRAM

    __image_copy_end = .; /* 重定位结束 */

    .sbss :
    {
    __bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.dynsbss)
        *(.scommon)
    } > SRAM

    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(.dynbss)
        *(COMMON)

    . = ALIGN(8);
    __bss_end = .;
    } > SRAM

    _end = .;

    /* Stabs debugging sections.  */
    .stab          0 : { *(.stab) }
    .stabstr       0 : { *(.stabstr) }
    .stab.excl     0 : { *(.stab.excl) }
    .stab.exclstr  0 : { *(.stab.exclstr) }
    .stab.index    0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment       0 : { *(.comment) }
    /* DWARF debug sections.
     * Symbols in the DWARF debugging sections are relative to the beginning
     * of the section so we begin them at 0.  */
    /* DWARF 1 */
    .debug          0 : { *(.debug) }
    .line           0 : { *(.line) }
    /* GNU DWARF 1 extensions */
    .debug_srcinfo  0 : { *(.debug_srcinfo) }
    .debug_sfnames  0 : { *(.debug_sfnames) }
    /* DWARF 1.1 and DWARF 2 */
    .debug_aranges  0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    /* DWARF 2 */
    .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_macinfo  0 : { *(.debug_macinfo) }
    /* SGI/MIPS DWARF 2 extensions */
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames  0 : { *(.debug_varnames) }
}
