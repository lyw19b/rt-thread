/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Decode bounded HID mouse descriptors independently of the host controller.
 */

#ifndef LS2K3000_HID_MOUSE_H
#define LS2K3000_HID_MOUSE_H

#include <stddef.h>
#include <stdint.h>

#define LS_MOUSE_REPORTS 8U
#define LS_MOUSE_FIELDS 32U
#define LS_MOUSE_REPORT_BYTES 64U

struct ls_mouse_field
{
    uint16_t offset;
    uint8_t bits;
    uint8_t kind;
    uint8_t report;
    uint8_t is_signed;
};

struct ls_mouse_layout
{
    struct ls_mouse_field field[LS_MOUSE_FIELDS];
    uint16_t bits[LS_MOUSE_REPORTS];
    uint8_t id[LS_MOUSE_REPORTS];
    uint8_t reports;
    uint8_t fields;
    uint8_t numbered;
};

struct ls_mouse_motion
{
    int32_t x;
    int32_t y;
    int32_t wheel;
    uint32_t buttons;
    uint32_t button_mask;
};

int ls_mouse_parse(struct ls_mouse_layout *layout, const uint8_t *data, size_t bytes);
int ls_mouse_decode(const struct ls_mouse_layout *layout, const uint8_t *data,
                     size_t bytes, struct ls_mouse_motion *motion);
void ls_mouse_boot_layout(struct ls_mouse_layout *layout);

#endif
