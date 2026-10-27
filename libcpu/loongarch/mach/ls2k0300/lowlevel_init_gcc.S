/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   Loongson platform definitions and initialization.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

//#include <config.h>
//#include <linux/linkage.h>
//#include <asm/regdef.h>
//#include <asm/addrspace.h>
//#include <mach/loongson.h>

#include "regdef.h"
#include "addrspace.h"
#include "linkage.h"
#include "rtconfig.h"
#include "loongarch.h"
#include "asm.h"
#include "early_print.h"

#include "loongson_board_info.h"
#include "loongson_soc_info.h"

#define CONFIG_SPL_BUILD 1

ENTRY(lowlevel_init)
    or      a4, ra, zero

    // 修复参考时钟计算错误的问题
    li.d    t0, 0x0001000107270e00
    csrwr   t0, LOONGARCH_CSR_MCSR2

    // 窗口不命中时，由内存控制器给出响应，防止CPU卡住。
    li.d    t0, LS_GENERAL_CFG0
    ld.w    t1, t0, 0x0
    li.w    t2, (1 << 5)
    or  t1, t1, t2
    st.w    t1, t0, 0x0

    /* spi speedup */
    li.d    t0, LS_SPI0_BASE
    li.w    t1, 0x47
    st.b    t1, t0, 0x4

    li.d    t0, LS_GENERAL_CFG0
    /*close usb & ddr soft reset: [0] ddr, [7] usbm*/
    li.w    t1, 0xffffff7e
    st.w    t1, t0, 0x1c

    /*unvalid ddr soft reset: [0] ddr*/
    ld.w    t1, t0, 0x1c
    ori     t1, t1, 0x1
    st.w    t1, t0, 0x1c

    /*[103:96] en [111:104] coherent*/
    /*0:cpu    1:usb     2:gmac0    3:gmac1   4:dc
      5:apb-dma(sdio spi can .etc)*/
    li.d    t0, LS_GENERAL_CFG3
    ld.w    t1, t0, 0x0
    li.w    t2, ((0x1f << 1) | (0x1f << 9))  //cache coherent for usb/gmac0/gmac1/dc
    or      t1, t1, t2
    st.w    t1, t0, 0x0

    /* select uart0 */
    /*cfg pins to main, uart0 use GPIO 40/41*/
    li.d    t0, LS_GPIO_32_47_MULTI_CFG
    li.w    t1, 0
    li.w    t2, 0xf0000
    st.w    t2, t0, 0x0

#ifdef SOC_LS2K300
    #include "clksetting_2k0300.S"
#endif

    //delay a while
    li.d    t1, 0x100
1:
    addi.d  t1, t1, -1
    bnez    t1, 1b
    nop

    or      ra, a4, zero
    jirl    zero, ra, 0
ENDPROC(lowlevel_init)


// Note: the Address mapping window setting up already.
//       so, use the Mapped address 0x8xxx... or 0x9xxx...
/*
 * Simple character printing routine used before full initialization
 */
//#define BAUDRATE        (115200)
//#define UART_REF_CLK  200000000
//#define UART_DIV_HI   (((UART_REF_CLK + (BAUDRATE*8)) / (BAUDRATE*16)) >> 8)
//#define UART_DIV_LO   (((UART_REF_CLK + (BAUDRATE*8)) / (BAUDRATE*16)) & 0xff)
ENTRY(init_serial)
    or      a4, ra, zero

    li.d    a0, CONSOLE_BASE_ADDR
    li.w    a1, 0x80
    st.b    a1, a0, 3

    li.w    a1, UART_DIV_HI
    st.b    a1, a0, 1
    li.w    a1, UART_DIV_LO
    st.b    a1, a0, 0
    li.w    a1, 3   #CFCR_8BITS
    st.b    a1, a0, 3

    li.w    a1, 71
    st.b    a1, a0, 2

    or      ra, a4, zero
    jirl    zero, ra, 0
ENDPROC(init_serial)

ENTRY(ram_init)
    or      s1, ra, zero

#if !defined(CONFIG_SPL) || defined(CONFIG_SPL_BUILD)
    // PRINTSTR("\r\nlock scache for early stack: ")
    // li.d a0, LOCK_CACHE_BASE
    // bl       printhex64
    // PRINTSTR(" - ")
    // li.d a0, LOCK_CACHE_BASE + LOCK_CACHE_SIZE
    // bl       printhex64

#ifndef BOOT_FROM_MEM
    li.d    t0, LS_SCACHE_LOCK_WIN0_BASE
    li.d    t1, ~(LOCK_CACHE_SIZE - 1)
    st.d    t1, t0, 0x40
    li.d    t1, (LOCK_CACHE_BASE & 0xffffffffffff) | (1 << 63)
    st.d    t1, t0, 0x0
#endif

#ifdef DBG_ASM
    PRINTSTR("\r\nLock Scache Done.\r\n")
#endif
#endif

#if defined(CONFIG_SPL_BUILD)
#ifdef DBG_ASM
    PRINTSTR("\r\nCopy spl code to locked scache...\r\n");
#endif

#ifndef BOOT_FROM_MEM
    // copy spl code to locked scache
    li.d    t0, BOOT_SPACE_BASE_UNCACHED
    la      t1, __text_start
    la      t2, __image_copy_end
1:
    ld.w    t3, t0, 0
    st.w    t3, t1, 0
    addi.d  t0, t0, 4
    addi.d  t1, t1, 4
    blt     t1, t2, 1b
#endif

    // clear bss
    la      t0, __bss_start
    la      t1, __bss_end
2:
    st.w    zero, t0, 0
    addi.d  t0, t0, 4
    blt     t0, t1, 2b

#ifndef BOOT_FROM_MEM
    // jump to cache
    la      t0, jump_cache
    jirl    zero, t0, 0
jump_cache:

    li.d    a0, LOCK_CACHE_BASE + LOCK_CACHE_SIZE
#endif

#endif // defined(CONFIG_SPL_BUILD)
#ifndef RT_USING_SMART
    PRINTSTR("\r\nram_init end...\r\n")
#endif
    or      ra, s1, zero

#ifndef BOOT_FROM_MEM
    // ra from 0x900000001cxxxxxx to be 0x90000009xxxxxxx
    ori     t1, zero, 0xff
    ori     t2, zero, 24
    sll.d   t1, t1, t2
    or      ra, ra, t1
    xor     ra, ra, t1
    ori     t1, zero, 0x98
    ori     t2, zero, 24
    sll.d   t1, t1, t2
    or      ra, ra, t1
#endif

    jirl    zero, ra, 0
ENDPROC(ram_init)


/******************************************************
 *used: a0 - char to print, a1, a2
 ******************************************************/
ENTRY(printchar)
    li.d    a1, CONSOLE_BASE_ADDR
1:
    ld.bu   a2, a1, 0x5
    andi    a2, a2, 0x20
    beqz    a2, 1b

    st.b    a0, a1, 0

    jirl    zero, ra, 0
ENDPROC(printchar)
