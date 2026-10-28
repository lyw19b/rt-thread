/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: ls2k_usb_camera.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Copy UVC payloads through the board-owned xHCI transport.
 */

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
