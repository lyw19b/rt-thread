/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-25     OpenLANPU Contributors    add bounded high-address USB host support
 *
 */

/* Description: Board-owned CherryUSB configuration; CPU buffers use bounce DMA. */
#ifndef LS2K_USB_CONFIG_H
#define LS2K_USB_CONFIG_H

#include <rtthread.h>

#if RT_TIMER_THREAD_STACK_SIZE < 1024
#error "USB hub timers require RT_TIMER_THREAD_STACK_SIZE >= 1024"
#endif

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#define CONFIG_USB_ALIGN_SIZE 8
#define USB_NOCACHE_RAM_SECTION
#define CONFIG_USBHOST_MAX_BUS 1
#define CONFIG_USBHOST_MAX_RHPORTS 1
#define CONFIG_USBHOST_MAX_EXTHUBS 1
#define CONFIG_USBHOST_MAX_EHPORTS 4
#define CONFIG_USBHOST_MAX_INTERFACES 8
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 2
#define CONFIG_USBHOST_MAX_ENDPOINTS 4
#define CONFIG_USBHOST_MAX_MSC_CLASS 4
#define CONFIG_USBHOST_DEV_NAMELEN 16
#define CONFIG_USBHOST_PSC_PRIO 12
#define CONFIG_USBHOST_PSC_STACKSIZE 8192
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN 512
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 1000
#define CONFIG_USBHOST_MSC_TIMEOUT 5000
#define CONFIG_USBHOST_MSOS_VENDOR_CODE 0

#endif
