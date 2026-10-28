/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: ls2k3000_usb.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Serialize controller ownership and deliver class notifications outside it.
 */

#ifndef LS2K3000_USB_H
#define LS2K3000_USB_H

struct ls_xhci;
void ls2k3000_usb_lock(void);
void ls2k3000_usb_unlock(void);
void ls2k3000_usb_camera_poll(struct ls_xhci *host);

#endif
