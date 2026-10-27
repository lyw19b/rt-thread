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

#include "rtconfig.h"

#if defined(SOC_LS2K300) || defined(BSP_LS2K0300)

#ifndef __MACH_LA_LS2K300_H
#define __MACH_LA_LS2K300_H

#ifndef __ASSEMBLY__
#include "ls2k_io.h"
#else
#define PHYS_TO_UNCACHED(address) (0x8000000000000000 | (address))
#define MHZ_TO_HZ                 1000000
#endif

/* CHIP CONFIG regs */
#define LS_GENERAL_CFG0  PHYS_TO_UNCACHED(0x16000100)
#define LS_GENERAL_CFG1  PHYS_TO_UNCACHED(0x16000104)
#define LS_GENERAL_CFG2  PHYS_TO_UNCACHED(0x16000108)
#define LS_GENERAL_CFG3  PHYS_TO_UNCACHED(0x1600010c)
#define LS_GENERAL_CFG4  PHYS_TO_UNCACHED(0x16000110)
#define LS_GENERAL_CFG5  PHYS_TO_UNCACHED(0x16000114)
#define LS_GENERAL_CFG6  PHYS_TO_UNCACHED(0x16000118)
#define LS_GENERAL_CFG7  PHYS_TO_UNCACHED(0x1600011c)
#define LS_GENERAL_CFG8  PHYS_TO_UNCACHED(0x16000120)
#define LS_GENERAL_CFG9  PHYS_TO_UNCACHED(0x16000124)
#define LS_GENERAL_CFG10 PHYS_TO_UNCACHED(0x16000128)
#define LS_GENERAL_CFG11 PHYS_TO_UNCACHED(0x1600012c)
#define LS_GENERAL_CFG12 PHYS_TO_UNCACHED(0x16000130)
#define LS_GENERAL_CFG13 PHYS_TO_UNCACHED(0x16000134)
#define LS_GENERAL_CFG14 PHYS_TO_UNCACHED(0x16000138)
#define LS_GENERAL_CFG15 PHYS_TO_UNCACHED(0x1600013c)
#define LS_SAMPLE_CFG0   PHYS_TO_UNCACHED(0x16000140)
#define LS_SAMPLE_CFG1   PHYS_TO_UNCACHED(0x16000144)
#define LS_SAMPLE_CFG2   PHYS_TO_UNCACHED(0x16000148)
#define LS_SAMPLE_CFG3   PHYS_TO_UNCACHED(0x1600014c)
#define LS_CHIP_HPT_LO   PHYS_TO_UNCACHED(0x16000150)
#define LS_CHIP_HPT_HI   PHYS_TO_UNCACHED(0x16000154)

#define LS_NODE_PLL_L PHYS_TO_UNCACHED(0x16000400)
#define LS_NODE_PLL_H PHYS_TO_UNCACHED(0x16000404)
#define LS_DDR_PLL_L  PHYS_TO_UNCACHED(0x16000408)
#define LS_DDR_PLL_H  PHYS_TO_UNCACHED(0x1600040c)
#define LS_PIX0_PLL   PHYS_TO_UNCACHED(0x16000410)
#define LS_PIX1_PLL   PHYS_TO_UNCACHED(0x16000414)
#define LS_FREQ_SCALE PHYS_TO_UNCACHED(0x16000420)

#define NODE_L1DIV_OUT_SHIFT   24
#define NODE_L1DIV_LOOPC_SHIFT 15
#define NODE_L1DIV_REF_SHIFT   8
#define NODE_L1DIV_OUT_WIDTH   7
#define NODE_L1DIV_LOOPC_WIDTH 9
#define NODE_L1DIV_REF_WIDTH   7
#define NODE_L1DIV_OUT_MARK    0x7f
#define NODE_L1DIV_LOOPC_MARK  0x1ff
#define NODE_L1DIV_REF_MARK    0x7f

#define NODE_L2DIV_OUT_I2S_SHIFT  8
#define NODE_L2DIV_OUT_GMAC_SHIFT 0
#define NODE_L2DIV_OUT_I2S_WIDTH  7
#define NODE_L2DIV_OUT_GMAC_WIDTH 7
#define NODE_L2DIV_OUT_I2S_MARK   0x7f
#define NODE_L2DIV_OUT_GMAC_MARK  0x7f

#define DDR_L1DIV_OUT_SHIFT   24
#define DDR_L1DIV_LOOPC_SHIFT 15
#define DDR_L1DIV_REF_SHIFT   8
#define DDR_L1DIV_OUT_WIDTH   7
#define DDR_L1DIV_LOOPC_WIDTH 9
#define DDR_L1DIV_REF_WIDTH   7
#define DDR_L1DIV_OUT_MARK    0x7f
#define DDR_L1DIV_LOOPC_MARK  0x1ff
#define DDR_L1DIV_REF_MARK    0x7f

#define DDR_L2DIV_OUT_MEMDIV_SHIFT 18
#define DDR_L2DIV_OUT_DEV_SHIFT    8
#define DDR_L2DIV_OUT_NET_SHIFT    0
#define DDR_L2DIV_OUT_MEMDIV_WIDTH 7
#define DDR_L2DIV_OUT_DEV_WIDTH    7
#define DDR_L2DIV_OUT_NET_WIDTH    7
#define DDR_L2DIV_OUT_MEMDIV_MARK  0x3
#define DDR_L2DIV_OUT_DEV_MARK     0x7f
#define DDR_L2DIV_OUT_NET_MARK     0x7f

#define PIX_L1DIV_OUT_SHIFT   24
#define PIX_L1DIV_LOOPC_SHIFT 15
#define PIX_L1DIV_REF_SHIFT   8
#define PIX_L1DIV_OUT_WIDTH   7
#define PIX_L1DIV_LOOPC_WIDTH 9
#define PIX_L1DIV_REF_WIDTH   7
#define PIX_L1DIV_OUT_MARK    0x7f
#define PIX_L1DIV_LOOPC_MARK  0x1ff
#define PIX_L1DIV_REF_MARK    0x7f

#define FREQSCALE_SDIO_SHIFT 24
#define FREQSCALE_I2S_SHIFT  20
#define FREQSCALE_APB_SHIFT  16
#define FREQSCALE_USB_SHIFT  12
#define FREQSCALE_BOOT_SHIFT 8
#define FREQSCALE_PIX_SHIFT  4
#define FREQSCALE_NODE_SHIFT 0
#define FREQSCALE_SDIO_MARK  0x7
#define FREQSCALE_I2S_MARK   0x7
#define FREQSCALE_APB_MARK   0x7
#define FREQSCALE_USB_MARK   0x7
#define FREQSCALE_BOOT_MARK  0x7
#define FREQSCALE_PIX_MARK   0x7
#define FREQSCALE_NODE_MARK  0x7

#define LS_GPIO_DIR_BIT_REG PHYS_TO_UNCACHED(0x16104000)
#define LS_GPIO_IN_BIT_REG  PHYS_TO_UNCACHED(0x16104020)
#define LS_GPIO_OUT_BIT_REG PHYS_TO_UNCACHED(0x16104010)

#define LS_GPIO_DIR_BYTE_REG PHYS_TO_UNCACHED(0x16104800)
#define LS_GPIO_OUT_BYTE_REG PHYS_TO_UNCACHED(0x16104900)
#define LS_GPIO_IN_BYTE_REG  PHYS_TO_UNCACHED(0x16104a00)

#define LS_GPIO_00_31_DIR  PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_32_63_DIR  PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_64_95_DIR  PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_96_105_DIR PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_00_31_IN   PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_32_63_IN   PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_00_31_OUT  PHYS_TO_UNCACHED(0x16004000)
#define LS_GPIO_32_63_OUT  PHYS_TO_UNCACHED(0x16004000)

#define LS_GPIO_0_15_MULTI_CFG   PHYS_TO_UNCACHED(0x16000490)
#define LS_GPIO_16_31_MULTI_CFG  PHYS_TO_UNCACHED(0x16000494)
#define LS_GPIO_32_47_MULTI_CFG  PHYS_TO_UNCACHED(0x16000498)
#define LS_GPIO_48_63_MULTI_CFG  PHYS_TO_UNCACHED(0x1600049c)
#define LS_GPIO_64_79_MULTI_CFG  PHYS_TO_UNCACHED(0x160004a0)
#define LS_GPIO_80_95_MULTI_CFG  PHYS_TO_UNCACHED(0x160004a4)
#define LS_GPIO_96_105_MULTI_CFG PHYS_TO_UNCACHED(0x160004a8)

#define LS_PIXCLK0_CTRL_REG PHYS_TO_UNCACHED(0x16000410)
#define LS_PIXCLK1_CTRL_REG PHYS_TO_UNCACHED(0x16000414)

#define PIXCLK_CTRL0_PSTDIV_SET (1 << 31)
#define PIXCLK_CTRL0_PSTDIV_PD  (1 << 30)
#define PIXCLK_CTRL0_PSTDIV_DF  (0X3f << 24)
#define PIXCLK_CTRL0_PLL_PD     (1 << 7)
#define PIXCLK_CTRL0_PLL_LDF    (0xff << 16)
#define PIXCLK_CTRL0_PLL_ODF    (0x3 << 5)
#define PIXCLK_CTRL0_PLL_IDF    (0X7 << 2)
#define PIXCLK_CTRL0_REF_SEL    0X3

#define writel_reg_bit(addr, clear_bit, bit_val) \
    (*(volatile unsigned int *)(addr)) = (*(volatile unsigned int *)(addr)) & (~clear_bit) | bit_val

/* OTG regs */

/* USB regs */
#define LS_EHCI_BASE PHYS_TO_UNCACHED(0x16080000)
#define LS_XHCI_BASE PHYS_TO_UNCACHED(0x16088000)

/* GMAC regs */

/* HDA regs */

/* GPU regs */

/* DC regs */
#define LS_DC_BASE          PHYS_TO_UNCACHED(0x16090000)
#define LS_FB_CFG_DVO_REG   (0x1240)
#define LS_FB_CFG_VGA_REG   (0x1250)
#define LS_FB_ADDR0_DVO_REG (0x1260)
#define LS_FB_ADDR0_VGA_REG (0x1270)
#define LS_FB_STRI_DVO_REG  (0x1280)
#define LS_FB_STRI_VGA_REG  (0x1290)
#define LS_FB_ADDR1_DVO_REG (0x1580)
#define LS_FB_ADDR1_VGA_REG (0x1590)

#define LS_FB_CUR_CFG_REG      (0x1520)
#define LS_FB_CUR_ADDR_REG     (0x1530)
#define LS_FB_CUR_LOC_ADDR_REG (0x1540)
#define LS_FB_CUR_BACK_REG     (0x1550)
#define LS_FB_CUR_FORE_REG     (0x1560)

#define LS_FB_DAC_CTRL_REG (0x1600)

/* SPI regs */
#define LS_SPI0_BASE PHYS_TO_UNCACHED(0x16010000)
#define LS_SPI1_BASE PHYS_TO_UNCACHED(0x16018000)
#define LS_SPI2_BASE PHYS_TO_UNCACHED(0x1610c000)
#define LS_SPI3_BASE PHYS_TO_UNCACHED(0x1610e000)

#define SPI_REF_CLK       ls2k_dev_apb_clock() // Peripheral clock inherited from firmware.
#define MAX_SPI_FLASH_NUM (1) // SPI控制器的最大编号

// 寄存器偏移
#define LS_SPI_SPCR_OFFSET       (0)     // 控制寄存器
#define LS_SPI_SPSR_OFFSET       (1)     // 状态寄存器
#define LS_SPI_TxFIFO_OFFSET     (2) // 发送的数据寄存器，与接收数据寄存器的偏移相同
#define LS_SPI_RxFIFO_OFFSET     (2) // 接收的数据寄存器，与发送数据寄存器的偏移相同
#define LS_SPI_SPER_OFFSET       (3) // 外部寄存器
#define LS_SPI_SPCS_OFFSET       (4) // 软件片选寄存器
#define LS_SPI_SFC_PARAM_OFFSET  (4) // 参数控制寄存器
#define LS_SPI_SFC_SOFTCS_OFFSET (5) // 片选控制寄存器
#define LS_SPI_SFC_TIMING_OFFSET (6) // 时序控制寄存器

// 寄存器SPCR中的位域
#define LS_SPI_SPCR_SPIE_BIT  (7)
#define LS_SPI_SPCR_SPIE_MASK (0x01 << LS_SPI_SPCR_SPIE_BIT)
#define LS_SPI_SPCR_SPE_BIT   (6)
#define LS_SPI_SPCR_SPE_MASK  (0x01 << LS_SPI_SPCR_SPE_BIT)
#define LS_SPI_SPCR_CPOL_BIT  (3)
#define LS_SPI_SPCR_CPOL_MASK (0x01 << LS_SPI_SPCR_CPOL_BIT)
#define LS_SPI_SPCR_CPHA_BIT  (2)
#define LS_SPI_SPCR_CPHA_MASK (0x01 << LS_SPI_SPCR_CPHA_BIT)
#define LS_SPI_SPCR_SPR_BIT   (0)
#define LS_SPI_SPCR_SPR_MASK  (0x03 << LS_SPI_SPCR_SPR_BIT)

// 寄存器SPSR中的位域
#define LS_SPI_SPSR_SPIF_BIT  (7)
#define LS_SPI_SPSR_SPIF_MASK (0x01 << LS_SPI_SPSR_SPIF_BIT)
#define LS_SPI_SPSR_WCOL_BIT  (6)
#define LS_SPI_SPSR_WCOL_MASK (0x01 << LS_SPI_SPSR_WCOL_BIT)

// 寄存器SPER中的位域
#define LS_SPI_SPER_SPRE_BIT  (0)
#define LS_SPI_SPER_SPRE_MASK (0x3 << LS_SPI_SPER_SPRE_BIT)

// 寄存器SFC_SOFTCS的位域
#define LS_SPI_SFC_SOFTCS_CSN_BIT   (4)
#define LS_SPI_SFC_SOFTCS_CSN_MASK  (0x0f << LS_SPI_SFC_SOFTCS_CSN_BIT)
#define LS_SPI_SFC_SOFTCS_CSEN_BIT  (0)
#define LS_SPI_SFC_SOFTCS_CSEN_MASK (0x0f << LS_SPI_SFC_SOFTCS_CSEN_BIT)

// 发送超时的门限值
#define LS_SPI_TX_TIMEOUT (10000)

/*SPI IO registers */
#define REG_CR1  0x00
#define REG_CR2  0x04
#define REG_CR3  0x08
#define REG_CR4  0x0c
#define REG_IER  0x10
#define REG_SR1  0x14
#define REG_SR2  0x18
#define REG_CFG1 0x20
#define REG_CFG2 0x24
#define REG_CFG3 0x28
#define REG_CRC1 0x30
#define REG_CRC2 0x34
#define REG_DR   0x40

#define SHIFTW(_x, _s) ((_x) << (_s))
#define SHIFTR(_x, _s) ((_x) >> (_s))

/* SPI_CR1 bit fields */
#define BIT_CR1_SPE     BIT(0)
#define BIT_CR1_CSTART  BIT(1)
#define BIT_CR1_AUTOSUS BIT(2)
#define BIT_CR1_SSREV   BIT(8)

/* SPI_CR2 bit fields */
#define BIT_CR2_RXDMAEN  BIT(7)
#define BIT_CR2_TXDMAEN  BIT(15)
#define MASK_CR2_RXFTHLV 0x3
#define MASK_CR2_TXFTHLV 0x300

/* SPI_IER bit fields */
#define BIT_IER_RXAIE  BIT(0)
#define BIT_IER_TXAIE  BIT(1)
#define BIT_IER_DXAIE  BIT(2)
#define BIT_IER_RXEIE  BIT(4)
#define BIT_IER_TXEIE  BIT(5)
#define BIT_IER_SUSPIE BIT(7)
#define BIT_IER_OVRIE  BIT(8)
#define BIT_IER_UDRIE  BIT(9)
#define BIT_IER_CRCEIE BIT(10)
#define BIT_IER_MODFIE BIT(11)
#define BIT_IER_EOTIE  BIT(15)
#define BIT_IER_ALL                                                                     \
    (BIT_IER_RXAIE | BIT_IER_TXAIE | BIT_IER_DXAIE | BIT_IER_RXEIE | BIT_IER_TXEIE |    \
     BIT_IER_SUSPIE | BIT_IER_OVRIE | BIT_IER_UDRIE | BIT_IER_CRCEIE | BIT_IER_MODFIE | \
     BIT_IER_EOTIE)

/* SPI_SR1 bit fields */
#define BIT_SR1_RXA  BIT(0)
#define BIT_SR1_TXA  BIT(1)
#define BIT_SR1_DXA  BIT(2)
#define BIT_SR1_RXE  BIT(4)
#define BIT_SR1_TXE  BIT(5)
#define BIT_SR1_SUSP BIT(7)
#define BIT_SR1_OVR  BIT(8)
#define BIT_SR1_UDR  BIT(9)
#define BIT_SR1_CRCE BIT(10)
#define BIT_SR1_MODF BIT(11)
#define BIT_SR1_EOT  BIT(15)

/* SPI_SR2 bit fields */
#define MASK_SR2_RXFLV 0x7 // << 0
#define MASK_SR2_TXFLV 0x700 // << 8

/* SPI_CFG1 bit fields */
#define BIT_CFG1_CPOL    BIT(0)
#define BIT_CFG1_CPHA    BIT(1)
#define BIT_CFG1_LSBFRST BIT(7)
#define MASK_CFG1_DSIZE  0x1f00 // << 8

/* SPI_CFG2 bit fields */
#define SHIFT_CFG2_BRINT 8
#define MASK_CFG2_BRDEC  0xfc // << 2
#define MASK_CFG2_BRINT  0xff00 // << 8

/* SPI_CFG3 bit fields */
#define BIT_CFG3_MSTR    BIT(0)
#define BIT_CFG3_DIOSWP  BIT(1)
#define BIT_CFG3_DIE     BIT(2)
#define BIT_CFG3_DOE     BIT(3)
#define MASK_CFG3_SSMODE 0x300 // << 8

/* SPI_CRC1 bit fields */
#define BIT_CRC1_CRCEN    BIT(0)
#define MASK_CRC1_CRCPOLY 0xfffffffe // << 1

/* SPI_CRC2 bit fields */
#define BIT_CRC2_RCRCINI  BIT(0)
#define BIT_CRC2_TCRCINI  BIT(1)
#define MASK_CRC2_CRCSIZE 0x1f00 // << 8

/* UART regs */
#define LS_UART0_REG_BASE PHYS_TO_UNCACHED(0x16100000)
#define LS_UART1_REG_BASE PHYS_TO_UNCACHED(0x16100400)
#define LS_UART2_REG_BASE PHYS_TO_UNCACHED(0x16100800)
#define LS_UART3_REG_BASE PHYS_TO_UNCACHED(0x16100c00)
#define LS_UART4_REG_BASE PHYS_TO_UNCACHED(0x16101000)
#define LS_UART5_REG_BASE PHYS_TO_UNCACHED(0x16101400)
#define LS_UART6_REG_BASE PHYS_TO_UNCACHED(0x16101800)
#define LS_UART7_REG_BASE PHYS_TO_UNCACHED(0x16101c00)
#define LS_UART8_REG_BASE PHYS_TO_UNCACHED(0x16102000)
#define LS_UART9_REG_BASE PHYS_TO_UNCACHED(0x16102400)

// 串口各寄存器相对基地址的偏移
#define LS2K_UART_DAT_OFFSET (0)
#define LS2K_UART_IER_OFFSET (1)
#define LS2K_UART_IIR_OFFSET (2)
#define LS2K_UART_FCR_OFFSET (2)
#define LS2K_UART_LCR_OFFSET (3)
#define LS2K_UART_MCR_OFFSET (4)
#define LS2K_UART_LSR_OFFSET (5)
#define LS2K_UART_MSR_OFFSET (6)

#define LS2K_UART_LSB_OFFSET (0) // 分频锁存器1
#define LS2K_UART_MSB_OFFSET (1) // 分频锁存器2

// 串口线路状态寄存器的位域
#define LS2K_UART_LSR_TE  (1 << 6)
#define LS2K_UART_LSR_TFE (1 << 5)

/* interrupt enable register */
#define IER_IRxE 0x1 /* 接收有效数据中断使能 */
#define IER_ITxE 0x2 /* 传输保存寄存器为空中断使能 */
#define IER_ILE  0x4 /* 接收器线路状态中断使能 */
#define IER_IME  0x8 /* Modem状态中断使能 */

#define IIR_RXTOUT 0xc
#define IIR_RXRDY  0x4
#define LSR_RXRDY  0x01

#define LCR_SPB (1 << 5)
#define LCR_EPS (1 << 4)
#define LCR_PE  (1 << 3)
#define LCR_SB  (1 << 2)
#define LCR_BEC (0)

// 串口波特率
#define UART_REF_CLK  (APB_FREQ * MHZ_TO_HZ)
#define UART_BAUDRATE (115200) // 波特率
#define UART_DIV_HI   (((UART_REF_CLK + (UART_BAUDRATE * 8)) / (UART_BAUDRATE * 16)) >> 8)
#define UART_DIV_LO   (((UART_REF_CLK + (UART_BAUDRATE * 8)) / (UART_BAUDRATE * 16)) & 0xff)

/* I2C regs */
// APB configured addr 0x1fe0,i2c0 addr is 0x1fe01000
#define LS_I2C0_REG_BASE PHYS_TO_UNCACHED(0x16108000)
#define LS_I2C1_REG_BASE PHYS_TO_UNCACHED(0x16109000)
#define LS_I2C2_REG_BASE PHYS_TO_UNCACHED(0x1610a000)
#define LS_I2C3_REG_BASE PHYS_TO_UNCACHED(0x1610b000)

#define MAX_I2C_NUM 3
#define I2C_REF_CLK ls2k_dev_apb_clock() // Peripheral clock inherited from firmware.

#define I2C_CR1   (0x00)
#define I2C_CR2   (0x04)
#define I2C_OAR   (0x08)
#define I2C_DR    (0x10)
#define I2C_SR1   (0x14)
#define I2C_SR2   (0x18)
#define I2C_CCR   (0x1c)
#define I2C_TRISE (0x20)

#define I2C_CR1_SWRST   (15)
#define I2C_CR1_RECOVER (14)
#define I2C_CR1_POS     (11)
#define I2C_CR1_ACK     (10)
#define I2C_CR1_STOP    (9)
#define I2C_CR1_START   (8)
#define I2C_CR1_PE      (0)

#define I2C_CR2_FREQ_MASK (0x3f)

/* Loongson I2C Status 1 */
#define I2C_SR1_AF   (10)
#define I2C_SR1_ARLO (9)
#define I2C_SR1_BERR (8)
#define I2C_SR1_TXE  (7)
#define I2C_SR1_RXNE (6)
#define I2C_SR1_BTF  (2)
#define I2C_SR1_ADDR (1)
#define I2C_SR1_SB   (0)

#define I2C_SR1_ITEVTEN_MASK ((1 << I2C_SR1_BTF) | (1 << I2C_SR1_ADDR) | (1 << I2C_SR1_SB))
#define I2C_SR1_ITBUFEN_MASK ((1 << I2C_SR1_TXE) | (1 << I2C_SR1_RXNE))
#define I2C_SR1_ITERREN_MASK ((1 << I2C_SR1_AF) | (1 << I2C_SR1_ARLO) | (1 << I2C_SR1_BERR))

/* Loongson I2C Status 2 */
#define I2C_SR2_BUSY (1)

#define I2C_CCR_F_S      (15)
#define I2C_CCR_DUTY     (14)
#define I2C_CCR_CCR_MASK (0xfff)

#define I2C_TRISE_TRISE_MASK (0x3f)

/* PWM regs */
#define LS_PWM0_REG_BASE PHYS_TO_UNCACHED(0x1611b000)
#define LS_PWM1_REG_BASE PHYS_TO_UNCACHED(0x1611b010)
#define LS_PWM2_REG_BASE PHYS_TO_UNCACHED(0x1611b020)
#define LS_PWM3_REG_BASE PHYS_TO_UNCACHED(0x1611b030)

#define PWM_LOW_BUFFER  (0x4)
#define PWM_FULL_BUFFER (0x8)
#define PWM_CTRL        (0xc)

#define PWM_INTE   (5)
#define PWM_RST    (7)
#define PWM_CAPTE  (8)
#define PWM_SINGLE (4) // 单脉冲控制位
#define PWM_INT    (6)
#define PWM_OE     (3)
#define PWM_EN     (0)
#define PWM_INVERT (9)
#define PWM_DZONE  (10)

#define PWM_REF_CLK ls2k_dev_apb_clock() // Peripheral clock inherited from firmware.

/* SDIO regs */
#define LS_SDIO0_BASE PHYS_TO_UNCACHED(0x1ff64000)
#define LS_SDIO1_BASE PHYS_TO_UNCACHED(0x1ff66000)

/* HPET regs */
#define LS_HPET0_BASE   PHYS_TO_UNCACHED(0x16120000)
#define LS_HPET0_PERIOD (LS_HPET0_BASE + 0x4)
#define LS_HPET0_CONF   (LS_HPET0_BASE + 0x10)
#define LS_HPET0_MAIN   (LS_HPET0_BASE + 0xF0)

#define LS_HPET1_BASE PHYS_TO_UNCACHED(0x16121000)
#define LS_HPET2_BASE PHYS_TO_UNCACHED(0x16122000)
#define LS_HPET3_BASE PHYS_TO_UNCACHED(0x16123000)

/* I2S regs */
#define LS_I2S_REG_BASE PHYS_TO_UNCACHED(0x16114000)

/* ACPI regs */
/*not use acpi */
#define NOT_USE_ACPI

#define LS_MAIN_WDT     PHYS_TO_UNCACHED(0x16124000)
#define LS_RST_CNT_REG  (LS_MAIN_WDT)
#define LS_WD_SET_REG   (LS_MAIN_WDT + 0x4)
#define LS_WD_TIMER_REG (LS_MAIN_WDT + 0x8)

/* DMA regs */
// #define LS_DMA_ORDER0 PHYS_TO_UNCACHED(0x16000c00)
// #define LS_DMA_ORDER1 PHYS_TO_UNCACHED(0x16000c10)
// #define LS_DMA_ORDER2 PHYS_TO_UNCACHED(0x16000c20)
// #define LS_DMA_ORDER3 PHYS_TO_UNCACHED(0x16000c30)

#define LS_DMA_CHANNEL_MAP0 PHYS_TO_UNCACHED(0x16000134)
#define LS_DMA_CHANNEL_MAP1 PHYS_TO_UNCACHED(0x16000138)
#define LS_DMA_CHANNEL_MAP2 PHYS_TO_UNCACHED(0x1600013c)

#define LS_DMA_ISR  PHYS_TO_UNCACHED(0x1612c000)
#define LS_DMA_IFCR PHYS_TO_UNCACHED(0x1612c004)
#define LS_DMA_CHANNEL_BASE(dma_channel_x) \
    PHYS_TO_UNCACHED(0x1612c000 + (0x8 + 0x14 * dma_channel_x))
#define LS_DMA_CHANNEL_CCR(dma_channel_x)   (LS_DMA_CHANNEL_BASE(dma_channel_x) + 0x0)
#define LS_DMA_CHANNEL_CNDTR(dma_channel_x) (LS_DMA_CHANNEL_BASE(dma_channel_x) + 0x4)
#define LS_DMA_CHANNEL_CPAR(dma_channel_x)  (LS_DMA_CHANNEL_BASE(dma_channel_x) + 0x8)
#define LS_DMA_CHANNEL_CMAR(dma_channel_x)  (LS_DMA_CHANNEL_BASE(dma_channel_x) + 0xc)

#define LS_DMA_IRQ_STATUS_MASK(dma_channel_x) (0xfU << (dma_channel_x * 4))
#define LS_DMA_IRQ_STATUS(val_x, dma_channel_x) \
    ((val_x & LS_DMA_IRQ_STATUS_MASK(dma_channel_x)) >> (dma_channel_x * 4))

#define LS_DMA_IRQ_END(status_x)   (status_x & 0x2)
#define LS_DMA_IRQ_HALF(status_x)  (status_x & 0x4)
#define LS_DMA_IRQ_ERROR(status_x) (status_x & 0x8)

#define LS_DMA_EN_OFFSET        (0)
#define LS_DMA_TCIE_OFFSET      (1)
#define LS_DMA_HTIE_OFFSET      (2)
#define LS_DMA_TEIE_OFFSET      (3)
#define LS_DMA_DIR_OFFSET       (4)
#define LS_DMA_CIRC_OFFSET      (5)
#define LS_DMA_PINC_OFFSET      (6)
#define LS_DMA_MINC_OFFSET      (7)
#define LS_DMA_P_SIZE_OFFSET    (8)
#define LS_DMA_M_SIZE_OFFSET    (10)
#define LS_DMA_PL_OFFSET        (12)
#define LS_DMA_MEM_2_MEM_OFFSET (14)

/* GPIO */
#define LS_GPIO_MAX             105
#define LS_GPIO_B_DIR_BASE      PHYS_TO_UNCACHED(0x16104800)
#define LS_GPIO_B_OUT_BASE      PHYS_TO_UNCACHED(0x16104900)
#define LS_GPIO_B_IN_BASE       PHYS_TO_UNCACHED(0x16104a00)
#define LS_GPIO_B_INT_EN_BASE   PHYS_TO_UNCACHED(0x16104b00)
#define LS_GPIO_B_INT_POL_BASE  PHYS_TO_UNCACHED(0x16104c00)
#define LS_GPIO_B_INT_EDGE_BASE PHYS_TO_UNCACHED(0x16104d00)
#define LS_GPIO_B_INT_CLR_BASE  PHYS_TO_UNCACHED(0x16104e00)
#define LS_GPIO_B_INT_STS_BASE  PHYS_TO_UNCACHED(0x16104f00)
#define LS_GPIO_B_INT_DUAL_BASE PHYS_TO_UNCACHED(0x16104f80)

#define LS_GPIO_LIO_IRQ_MAP_LEN     7
#define LS_GPIO_START_IRQ           53 // GPIO中断的起始编号
#define LS_GPIO_END_IRQ             59 // GPIO中断的终止编号
#define LS_GPIO_IRQ_SHARED_INTERVAL 16 // 16个GPIO共享一个中断

/* RTC regs */
#define LS_RTC_REG_BASE   PHYS_TO_UNCACHED(0x16128000)
#define LS_TOY_TRIM_REG   (LS_RTC_REG_BASE + 0x0020)
#define LS_TOY_WRITE0_REG (LS_RTC_REG_BASE + 0x0024)
#define LS_TOY_WRITE1_REG (LS_RTC_REG_BASE + 0x0028)
#define LS_TOY_READ0_REG  (LS_RTC_REG_BASE + 0x002c)
#define LS_TOY_READ1_REG  (LS_RTC_REG_BASE + 0x0030)
#define LS_TOY_MATCH0_REG (LS_RTC_REG_BASE + 0x0034)
#define LS_TOY_MATCH1_REG (LS_RTC_REG_BASE + 0x0038)
#define LS_TOY_MATCH2_REG (LS_RTC_REG_BASE + 0x003c)
#define LS_RTC_CTRL_REG   (LS_RTC_REG_BASE + 0x0040)
#define LS_RTC_TRIM_REG   (LS_RTC_REG_BASE + 0x0060)
#define LS_RTC_WRITE0_REG (LS_RTC_REG_BASE + 0x0064)
#define LS_RTC_READ0_REG  (LS_RTC_REG_BASE + 0x0068)
#define LS_RTC_MATCH0_REG (LS_RTC_REG_BASE + 0x006c)
#define LS_RTC_MATCH1_REG (LS_RTC_REG_BASE + 0x0070)
#define LS_RTC_MATCH2_REG (LS_RTC_REG_BASE + 0x0074)

#define LS_SCACHE_LOCK_WIN0_BASE PHYS_TO_UNCACHED(0x16000200)
#define LS_SCACHE_LOCK_WIN1_BASE PHYS_TO_UNCACHED(0x16000208)
#define LS_SCACHE_LOCK_WIN2_BASE PHYS_TO_UNCACHED(0x16000210)
#define LS_SCACHE_LOCK_WIN3_BASE PHYS_TO_UNCACHED(0x16000218)
#define LS_SCACHE_LOCK_WIN0_MASK PHYS_TO_UNCACHED(0x16000240)
#define LS_SCACHE_LOCK_WIN1_MASK PHYS_TO_UNCACHED(0x16000248)
#define LS_SCACHE_LOCK_WIN2_MASK PHYS_TO_UNCACHED(0x16000250)
#define LS_SCACHE_LOCK_WIN3_MASK PHYS_TO_UNCACHED(0x16000258)

/*thermal*/
#define LS_THERMAL_REG_BASE          PHYS_TO_UNCACHED(0x16001500)
#define LOONGSON2_MAX_SENSOR_SEL_NUM 3

#define LOONGSON2_THSENS_CTRL_HI_REG  0x0
#define LOONGSON2_THSENS_CTRL_LOW_REG 0x8
#define LOONGSON2_THSENS_STATUS_REG   0x10
#define LOONGSON2_THSENS_OUT_REG      0x14
#define LOONGSON2_THSENS_CFG_REG      0x18

#define LOONGSON2_THSENS_INT_LO   BIT(0)
#define LOONGSON2_THSENS_INT_HIGH BIT(1)
#define LOONGSON2_THSENS_INT_EN   (LOONGSON2_THSENS_INT_LO | LOONGSON2_THSENS_INT_HIGH)
#define LOONGSON2_THSENS_OUT_MASK 0xFF

/*ADC*/
#define LS_ADC_REG_BASE PHYS_TO_UNCACHED(0x1611c000)

#define SR    0x00
#define CR1   0x04
#define CR2   0x08
#define SMPR1 0x0c
#define SMPR2 0x10
#define JOFR1 0x14
#define JOFR2 0x18
#define JOFR3 0x1C
#define JOFR4 0x20
#define HTR   0x24
#define LTR   0x28
#define SQR1  0x2C
#define SQR2  0x30
#define SQR3  0x34
#define JSQR  0x38
#define JDR1  0x3C
#define JDR2  0x40
#define JDR3  0x44
#define JDR4  0x48
#define DR    0x4c

#define LS_ADC_SR_REG    (LS_ADC_REG_BASE + 0x0)
#define LS_ADC_CR1_REG   (LS_ADC_REG_BASE + 0x4)
#define LS_ADC_CR2_REG   (LS_ADC_REG_BASE + 0x8)
#define LS_ADC_SMPR1_REG (LS_ADC_REG_BASE + 0x0c)
#define LS_ADC_SMPR2_REG (LS_ADC_REG_BASE + 0x10)
#define LS_ADC_JOFR1_REG (LS_ADC_REG_BASE + 0x14)
#define LS_ADC_JOFR2_REG (LS_ADC_REG_BASE + 0x18)
#define LS_ADC_JOFR3_REG (LS_ADC_REG_BASE + 0x1c)
#define LS_ADC_JOFR4_REG (LS_ADC_REG_BASE + 0x20)
#define LS_ADC_HTR_REG   (LS_ADC_REG_BASE + 0x24)
#define LS_ADC_LTR_REG   (LS_ADC_REG_BASE + 0x28)
#define LS_ADC_SQR1_REG  (LS_ADC_REG_BASE + 0x2c)
#define LS_ADC_SQR2_REG  (LS_ADC_REG_BASE + 0x30)
#define LS_ADC_SQR3_REG  (LS_ADC_REG_BASE + 0x34)
#define LS_ADC_JSQR_REG  (LS_ADC_REG_BASE + 0x38)
#define LS_ADC_JDR1_REG  (LS_ADC_REG_BASE + 0x3c)
#define LS_ADC_JDR2_REG  (LS_ADC_REG_BASE + 0x40)
#define LS_ADC_JDR3_REG  (LS_ADC_REG_BASE + 0x44)
#define LS_ADC_JDR4_REG  (LS_ADC_REG_BASE + 0x48)
#define LS_ADC_DR_REG    (LS_ADC_REG_BASE + 0x4c)

/*cfg4 pad*/
#define PAD_CTRL_JTAG  8
#define PAD_CTRL_DVO   10
#define PAD_CTRL_UART  12
#define PAD_CTRL_GMAC  14
#define PAD_CTRL_SDIO  16
#define PAD_CTRL_SPI   18
#define PAD_CTRL_I2S   20
#define PAD_CTRL_TIMER 22
#define PAD_CTRL_USB   24
#define PAD_CTRL_EMMC  26

/* Core INT regs */
#define CORE_IPISR_REG   PHYS_TO_UNCACHED(0x16001000) // RO NA 处理器核的 IPI_Status 寄存器
#define CORE_IPIEN_REG   PHYS_TO_UNCACHED(0x16001004) // RW 0x0 处理器核的 IPI_Enalbe 寄存器
#define CORE_IPISET_REG  PHYS_TO_UNCACHED(0x16001008) // WO NA 处理器核的 IPI_Set 寄存器
#define CORE_IPI_CLR_REG PHYS_TO_UNCACHED(0x1600100c) // WO NA 处理器核的 IPI_Clear 寄存器
#define CORE_INTISR0_REG PHYS_TO_UNCACHED(0x16001040) // 路由给CORE的低32位中断状态
#define CORE_INTISR1_REG PHYS_TO_UNCACHED(0x16001048) // 路由给CORE的高32位中断状态

/* LIO INT regs */
#define INTISR_0_REG  PHYS_TO_UNCACHED(0x16001420) // 低32位中断状态寄存器
#define INTSET_0_REG  PHYS_TO_UNCACHED(0x16001428) // 低32位设置使能寄存器
#define INTIEN_0_REG  PHYS_TO_UNCACHED(0x16001424) // 低32位中断使能状态寄存器
#define INTPOL_0_REG  PHYS_TO_UNCACHED(0x16001430) // 低32位极性设置寄存器(电平中断)
#define INTEDGE_0_REG PHYS_TO_UNCACHED(0x16001434) // 低32位触发方式寄存器
#define INTCLR_0_REG \
    PHYS_TO_UNCACHED(0x1600142c) // 低32位中断清除寄存器,清除使能寄存器和脉冲触发的中断

#define INTISR_1_REG  PHYS_TO_UNCACHED(0x16001460) // 高32位中断状态寄存器
#define INTSET_1_REG  PHYS_TO_UNCACHED(0x16001468) // 高32位设置使能寄存器
#define INTIEN_1_REG  PHYS_TO_UNCACHED(0x16001464) // 高32位中断使能状态寄存器
#define INTPOL_1_REG  PHYS_TO_UNCACHED(0x16001470) // 高32位极性设置寄存器(电平中断)
#define INTEDGE_1_REG PHYS_TO_UNCACHED(0x16001474) // 高32位触发方式寄存器
#define INTCLR_1_REG \
    PHYS_TO_UNCACHED(0x1600146c) // 高32位中断清除寄存器,清除使能寄存器和脉冲触发的中断

#define ENTRY0_0_REG  PHYS_TO_UNCACHED(0x16001400) // 8位中断路由寄存器[0--7]
#define ENTRY8_0_REG  PHYS_TO_UNCACHED(0x16001408) // 8位中断路由寄存器[8--15]
#define ENTRY16_0_REG PHYS_TO_UNCACHED(0x16001410) // 8位中断路由寄存器[16--23]
#define ENTRY24_0_REG PHYS_TO_UNCACHED(0x16001418) // 8位中断路由寄存器[24--31]

#define ENTRY0_1_REG  PHYS_TO_UNCACHED(0x16001440) // 8位中断路由寄存器[32--39]
#define ENTRY8_1_REG  PHYS_TO_UNCACHED(0x16001448) // 8位中断路由寄存器[40--47]
#define ENTRY16_1_REG PHYS_TO_UNCACHED(0x16001450) // 8位中断路由寄存器[48--55]
#define ENTRY24_1_REG PHYS_TO_UNCACHED(0x16001458) // 8位中断路由寄存器[56--63]

/* S3 Need */
/*
#define STR_XBAR_CONFIG_NODE_a0(OFFSET, BASE, MASK, MMAP) \
        daddi   v0, t0, OFFSET;     \
        dli     t1, BASE;           \
        or      t1, t1, a0;         \
        sd      t1, 0x00(v0);       \
        dli     t1, MASK;           \
        sd      t1, 0x40(v0);       \
        dli     t1, MMAP;           \
        sd      t1, 0x80(v0);
*/

#define LS2K0300_LIO_UART0_IRQ   (0)
#define LS2K0300_LIO_UART1_IRQ   (1)
#define LS2K0300_LIO_UART2_5_IRQ (2)
#define LS2K0300_LIO_UART6_9_IRQ (3)

#define LS2K0300_WATCHDOG_BASE PHYS_TO_UNCACHED(0x16124000)

#endif /* __MACH_LA_LS2K300_H */
#endif
