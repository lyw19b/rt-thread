/*
 * Copyright (c) 2026, OpenLANPU Contributors
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-27     OpenLANPU Contributors    define the board/panel working point
 *
 * Description:
 *   Hummingbird GBKPAW0 / ATK-MD0700R-1024x600 parallel RGB configuration.
 * Notes:
 *   120 MHz comes from the core schematic. Geometry and the 45 MHz working
 *   point match the ATK F429 example. The panel's typical point is 51.2 MHz.
 *   Fine-detail optical acceptance is still pending at this working point.
 *   MODE is strapped for DE on the ATK V1.6 schematic. PCLK phase below is
 *   a controller bit, not a measured panel sampling edge. LCD RESET follows
 *   PLTRESETN; GPIO84 controls backlight only. No software panel reset exists.
 */

#ifndef HUMMINGBIRD_PANEL_H
#define HUMMINGBIRD_PANEL_H

#define HB_PANEL_NAME             "ATK-MD0700R-1024x600"
#define HB_PANEL_PROFILE          "atk-45m-phase1-diagnostic-v1"
#define HB_PANEL_REF_HZ           120000000ULL
#define HB_PANEL_PIXEL_HZ         45000000ULL
#define HB_PANEL_TYPICAL_HZ       51200000ULL
#define HB_PANEL_PLL_REF_DIV      6U
#define HB_PANEL_PLL_LOOP         72U
#define HB_PANEL_PLL_OUT_DIV      32U
#define HB_PANEL_PLL_PARAMETERS   ((HB_PANEL_PLL_OUT_DIV << 24) | \
                                  (HB_PANEL_PLL_LOOP << 15) | \
                                  (HB_PANEL_PLL_REF_DIV << 8))

#define HB_PANEL_WIDTH            1024U
#define HB_PANEL_HEIGHT           600U
#define HB_PANEL_HFRONT           160U
#define HB_PANEL_HSYNC            20U
#define HB_PANEL_HBACK            140U
#define HB_PANEL_VFRONT           12U
#define HB_PANEL_VSYNC            3U
#define HB_PANEL_VBACK            20U

/* Preserve the established vendor bus word. Reserved/read-only bits are not
 * interpreted here; the live readback is 0x80000301 on this board. */
#define HB_PANEL_DVO_CONFIG       (0x80001111U | (1U << 9))
#define HB_PANEL_PAD_MASK         (3U << 10)
#define HB_PANEL_PAD_VALUE        (3U << 10) /* Manual 3.4.5: 12 mA. */

#if HB_PANEL_REF_HZ * HB_PANEL_PLL_LOOP != \
    HB_PANEL_PIXEL_HZ * HB_PANEL_PLL_REF_DIV * HB_PANEL_PLL_OUT_DIV
#error Hummingbird pixel clock and PLL parameters disagree
#endif

#endif
