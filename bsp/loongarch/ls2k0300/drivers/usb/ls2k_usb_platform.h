/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     LoongsonLab               share EHCI through board resources
 *
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/* Description: Firmware resource handoff for the board-owned EHCI adapter. */

#ifndef LS2K_USB_PLATFORM_H
#define LS2K_USB_PLATFORM_H

#include <rtthread.h>

rt_err_t ls2k_usb_platform_init(rt_ubase_t *base, int *irq, unsigned int *hardware_ports);

#endif
