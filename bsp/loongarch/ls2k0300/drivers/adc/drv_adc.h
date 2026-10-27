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
 *   LS2K0300 development BSP: drv adc.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __DRV_ADC_H__
#define __DRV_ADC_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <io.h>
#include "ls2k300.h"

/* ADC 寄存器偏移 */
#define ADC_SR_OFFSET    0x00
#define ADC_CR1_OFFSET   0x04
#define ADC_CR2_OFFSET   0x08
#define ADC_SMPR1_OFFSET 0x0C
#define ADC_SMPR2_OFFSET 0x10
#define ADC_SQR1_OFFSET  0x2C
#define ADC_SQR2_OFFSET  0x30
#define ADC_SQR3_OFFSET  0x34
#define ADC_JSQR_OFFSET  0x38
#define ADC_JDR1_OFFSET  0x3C
#define ADC_JDR2_OFFSET  0x40
#define ADC_JDR3_OFFSET  0x44
#define ADC_JDR4_OFFSET  0x48
#define ADC_DR_OFFSET    0x4C

/* ADC 寄存器基地址 */
#define LS_ADC_REG_BASE PHYS_TO_UNCACHED(0x1611c000)

/* CR1 寄存器位域 */
#define ADC_CR1_OPS_SHIFT     30
#define ADC_CR1_CLKDIV_SHIFT  24
#define ADC_CR1_DISCNUM_SHIFT 13
#define ADC_CR1_JDISCEN_SHIFT 12
#define ADC_CR1_DISCEN_SHIFT  11
#define ADC_CR1_JAUTO_SHIFT   10
#define ADC_CR1_SCAN_SHIFT    8
#define ADC_CR1_EOCIE_SHIFT   5

/* CR2 寄存器位域 */
#define ADC_CR2_ADON_SHIFT     0
#define ADC_CR2_CONT_SHIFT     1
#define ADC_CR2_CAL_SHIFT      2
#define ADC_CR2_RSTCAL_SHIFT   3
#define ADC_CR2_DMA_SHIFT      8
#define ADC_CR2_ALIGN_SHIFT    11
#define ADC_CR2_JEXTSEL_SHIFT  12
#define ADC_CR2_JEXTTRIG_SHIFT 15
#define ADC_CR2_EXTSEL_SHIFT   17
#define ADC_CR2_EXTTRIG_SHIFT  20
#define ADC_CR2_JSWSTART_SHIFT 21
#define ADC_CR2_SWSTART_SHIFT  22
#define ADC_CR2_JTRIGMOD_SHIFT 24
#define ADC_CR2_CLKDIV_SHIFT   26
#define ADC_CR2_ADCEDGE_SHIFT  30

/* SR 寄存器位掩码 */
#define ADC_SR_STRT  (1 << 4)
#define ADC_SR_JSTRT (1 << 3)
#define ADC_SR_JEOC  (1 << 2)
#define ADC_SR_EOC   (1 << 1)

/* ADC 通道定义 */
#define ADC_CHANNEL_0   0x00
#define ADC_CHANNEL_1   0x01
#define ADC_CHANNEL_2   0x02
#define ADC_CHANNEL_3   0x03
#define ADC_CHANNEL_4   0x04
#define ADC_CHANNEL_5   0x05
#define ADC_CHANNEL_6   0x06
#define ADC_CHANNEL_7   0x07
#define ADC_CHANNEL_MAX 7

/* ADC 采样时间定义 */
#define ADC_SAMPLETIME_1CYCLES   0x00
#define ADC_SAMPLETIME_2CYCLES   0x01
#define ADC_SAMPLETIME_4CYCLES   0x02
#define ADC_SAMPLETIME_8CYCLES   0x03
#define ADC_SAMPLETIME_16CYCLES  0x04
#define ADC_SAMPLETIME_32CYCLES  0x05
#define ADC_SAMPLETIME_64CYCLES  0x06
#define ADC_SAMPLETIME_128CYCLES 0x07

/* 默认采样时间 */
#define ADC_DEFAULT_SAMPLETIME ADC_SAMPLETIME_128CYCLES

/* ADC 分辨率 (12位) */
#define ADC_RESOLUTION 12
#define ADC_MAX_VALUE  ((1 << ADC_RESOLUTION) - 1)

/* 参考电压，单位 mV (典型值 1.8V) */
#define ADC_VREF_MV 1800

/* LS2K300 ADC 设备结构体 */
struct ls2k300_adc_device
{
    struct rt_adc_device parent;
    rt_uint64_t reg_base;
    rt_int16_t vref_mv;
    rt_uint8_t resolution;
    rt_bool_t initialized;
};

int rt_hw_adc_init(void);

#endif /* __DRV_ADC_H__ */
