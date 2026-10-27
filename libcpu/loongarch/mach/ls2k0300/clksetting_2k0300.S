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

#ifndef _KERNEL
#define _KERNEL
#endif

#include "loongarch.h"
#include "regdef.h"
#include "addrspace.h"
#include "linkage.h"

#include "loongson_board_info.h"
#include "loongson_soc_info.h"

/* NODE PLL */
#define GMAC_FREQ   125
#define I2S_FREQ    100
#define NETWORK_FREQ 400

#ifdef REF_120M
#define NODE_REFC   6
#define NODE_DIV    2
#else
#define NODE_REFC   4
#define NODE_DIV    2
#endif
#define NODE_LOOPC  (CORE_FREQ*NODE_REFC*NODE_DIV/REF_FREQ)

#define GMAC_DIV    (CORE_FREQ*NODE_DIV/GMAC_FREQ)
#define I2S_DIV     (CORE_FREQ*NODE_DIV/I2S_FREQ)

/* DDR PLL */
//#define DDR_FREQ  800                 //MEM 800~1200Mhz
#ifdef REF_120M
#define DDR_REFC    3
#define DDR_DIV     2
#else
#define DDR_REFC    4
#define DDR_DIV     4
#endif
#define DDR_LOOPC   (DDR_FREQ*DDR_REFC*DDR_DIV/REF_FREQ)
#define MEMDIV_MODE 1
#define DEV_DIV     (DDR_FREQ*DDR_DIV/APB_FREQ) //APB 100M
#define NETWORK_DIV (DDR_FREQ*DDR_DIV/NET_FREQ) //NETWORK400MHz

/* PIX PLL */
#define PIX_REFC    4
#define PIX_LOOPC   80 //PIX_FREQ 100M
#define PIX_DIV     20
#define GMACBP_DIV  16 //GMAC_FREQ 125M

#if ((DDR_LOOPC > 255) | (NODE_LOOPC > 255) | (SOC_LOOPC > 255))
PLL LOOPC overflow
#endif

#define SEL_PLL0    (0x1)
#define SEL_PLL1    (0x2)
#define SEL_PLL2    (0x4)
#define PLL_L1_ENA  (0x1 << 3)
#define PLL_L1_PD_PLL   (0x1 << 5)
#define PLL_L1_LOCKED   (0x1 << 7)

    # TTYDBG ("Soft CLK SEL adjust begin\r\n")
    # TTYDBG ("\r\nNODE :")

    li.d    t0, LS_NODE_PLL_L
    li.w    t1, PLL_L1_PD_PLL   //power down pll L1 first
    st.w    t1, t0, 0
    li.w    t4, (NODE_DIV << NODE_L1DIV_OUT_SHIFT) | (NODE_LOOPC << NODE_L1DIV_LOOPC_SHIFT) | (NODE_REFC << NODE_L1DIV_REF_SHIFT)
    st.w    t4, t0, 0

    ld.w    t1, t0, 4
    li.w    t2, ~((NODE_L2DIV_OUT_I2S_MARK << NODE_L2DIV_OUT_I2S_SHIFT) | NODE_L2DIV_OUT_GMAC_MARK)
    and     t1, t1, t2
    li.w    t2, (I2S_DIV << NODE_L2DIV_OUT_I2S_SHIFT) | (GMAC_DIV)
    or      t2, t2, t1
    st.w    t2, t0, 0x4

    ori     t4, t4, PLL_L1_ENA
    st.w    t4, t0, 0

11:
    ld.w    a0, t0, 0
    li.w    a1, PLL_L1_LOCKED
    and     a0, a1, a0
    beqz    a0, 11b //wait_locked_sys

    ld.w    a0, t0, 0
    ori     a0, a0, SEL_PLL0 | SEL_PLL1 | SEL_PLL2
    st.w    a0, t0, 0

    # bl    hexserial

    # TTYDBG ("\r\nDDR  :")

    li.d    t0, LS_DDR_PLL_L
    ld.w    t1, t0, 4
    li.w    t2, ~(DDR_L2DIV_OUT_MEMDIV_MARK << DDR_L2DIV_OUT_MEMDIV_SHIFT)
    and     t1, t1, t2
    li.w    t2, (MEMDIV_MODE << DDR_L2DIV_OUT_MEMDIV_SHIFT)
    or      t2, t2, t1
    st.w    t2, t0, 0x4

/*mem div reset*/
    ld.w    t1, t0, 4
    li.w    t2, ~(1 << 16)
    and     t2, t1, t2
    st.w    t2, t0, 0x4
    li.w    t2, (1 << 16)
    or      t2, t2, t1
    st.w    t2, t0, 0x4

    li.d    t0, LS_DDR_PLL_L
    li.w    t1, PLL_L1_PD_PLL   //power down pll first
    st.w    t1, t0, 0
    li.w    t4, (DDR_DIV << DDR_L1DIV_OUT_SHIFT) | (DDR_LOOPC << DDR_L1DIV_LOOPC_SHIFT) | (DDR_REFC << DDR_L1DIV_REF_SHIFT)
    st.w    t4, t0, 0

    ld.w    t1, t0, 4
    li.w    t2, ~((DDR_L2DIV_OUT_DEV_MARK << DDR_L2DIV_OUT_DEV_SHIFT) | DDR_L2DIV_OUT_NET_MARK)
    and     t1, t1, t2
    li.w    t2, (DEV_DIV << DDR_L2DIV_OUT_DEV_SHIFT) | (NETWORK_DIV)
    or      t2, t2, t1
    st.w    t2, t0, 0x4

    ori     t4, t4, PLL_L1_ENA
    st.w    t4, t0, 0

21:
    ld.w    a0, t0, 0
    li.w    a1, PLL_L1_LOCKED
    and     a0, a0, a1
    beqz    a0, 21b //wait_locked_ddr

    ld.w    a0, t0, 0
    ori     a0, a0, SEL_PLL0 | SEL_PLL1 | SEL_PLL2
    st.w    a0, t0, 0

    # bl    hexserial
#if 0
    TTYDBG ("\r\nPIX    :")

    li.d    t0, LS_PIX0_PLL
    li.w    t1, PLL_L1_PD_PLL   //power down pll first
    st.w    t1, t0, 0
    li.w    t1, (PIX_DIV << 24) | (PIX_LOOPC << 15) | (PIX_REFC << 8)
    st.w    t1, t0, 0

    ld.w    t1, t0, 4
    li.w    t2, ~((0x7f << 8) | 0x7f)
    and     t1, t1, t2
    li.w    t2, GMACBP_DIV
    or      t2, t2, t1
    st.w    t2, t0, 0x4

    ld.w    t1, t0, 0
    ori     t1, t1, PLL_L1_ENA
    st.w    t1, t0, 0

21:
    ld.w    a0, t0, 0
    li.w    a1, PLL_L1_LOCKED
    and     a0, a0, a1
    beqz    a0, 21b //wait_locked_ddr

    ld.w    a0, t0, 0
    ori     a0, a0, SEL_PLL0 | SEL_PLL1
    st.w    a0, t0, 0

    bl      hexserial
#endif
    # TTYDBG ("\r\nPLL CONFIGURE DONE!\r\n")
