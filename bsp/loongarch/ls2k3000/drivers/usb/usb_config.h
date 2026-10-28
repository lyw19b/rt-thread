/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: usb_config.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Describe the project UVC class API without starting another host stack.
 */

#ifndef LS2K3000_USB_CONFIG_H
#define LS2K3000_USB_CONFIG_H

#include <rtthread.h>

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#define CONFIG_USB_ALIGN_SIZE 8
#define USB_NOCACHE_RAM_SECTION
#define CONFIG_USBHOST_MAX_BUS 2
#define CONFIG_USBHOST_MAX_RHPORTS 8
#define CONFIG_USBHOST_MAX_EXTHUBS 1
#define CONFIG_USBHOST_MAX_EHPORTS 1
#define CONFIG_USBHOST_MAX_INTERFACES 8
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 32
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN 4096
#define CONFIG_USBHOST_MAX_ENDPOINTS 4
#define CONFIG_USBHOST_DEV_NAMELEN 16
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 1000

#endif
