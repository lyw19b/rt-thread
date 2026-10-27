/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: GPL-1.0+
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   Loongson platform definitions and initialization.
 */

#include "rtconfig.h"

#if defined(SOC_LS2K300) || defined(BSP_LS2K0300)

#ifndef __LOONGSON_LA_COMMON_H__
#define __LOONGSON_LA_COMMON_H__

/* Loongson LS2K300 clock configuration. */

#define REF_120M

#ifdef REF_120M
#define REF_FREQ 120  // 参考时钟固定为120MHz
#else
#define REF_FREQ 100  // 参考时钟固定为100MHz
#endif

#define CORE_FREQ 1000 // CPU use 1000MHz
#define DDR_FREQ  800  // not use DDR just use 800
#define NET_FREQ  200  // NETWORK 200~400MHz, for NETWORK, DC, PRINT
#ifdef __ASSEMBLY__
#define APB_FREQ 200
#else
unsigned long ls2k_dev_apb_clock(void);
#define APB_FREQ (ls2k_dev_apb_clock() / 1000000UL)
#endif

/* SOC PLL configuration - placeholder for clksetting_2k300.S */
#define SOC_LOOPC 0

/* UART configuration */
#define CONSOLE_BASE_ADDR LS_UART0_REG_BASE
/* NS16550-ish UARTs */
#define CONFIG_SYS_NS16550_CLK (APB_FREQ * MHZ_TO_HZ) // CLK_in: 200MHz

#define UART_FREQ (APB_FREQ)

#endif /* __LOONGSON_LA_COMMON_H__ */
#endif
