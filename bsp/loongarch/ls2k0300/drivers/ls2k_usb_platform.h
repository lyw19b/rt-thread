/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    share EHCI through board resources
 *
 */

/* Description: Firmware resource handoff for the board-owned EHCI adapter. */
#ifndef LS2K_USB_PLATFORM_H
#define LS2K_USB_PLATFORM_H

#include <rtthread.h>

rt_err_t ls2k_usb_platform_init(rt_ubase_t *base, int *irq,
                               unsigned int *hardware_ports);

#endif
