/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     OpenLANPU Contributors    add board-owned USB camera support
 *
 */

/* Description: Board transport for the project UVC class; all buffers are copied. */
#ifndef LS2K_USB_CAMERA_H
#define LS2K_USB_CAMERA_H

#include <stddef.h>
#include <stdint.h>
struct usbh_hubport;
int ls2k_usb_camera_start(struct usbh_hubport *port, uint8_t endpoint,
                          uint16_t packet, uint8_t interval);
int ls2k_usb_camera_stop(struct usbh_hubport *port);
int ls2k_usb_camera_read(struct usbh_hubport *port, uint8_t *data, size_t capacity);

#endif
