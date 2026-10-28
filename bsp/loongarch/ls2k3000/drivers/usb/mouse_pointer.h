/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add bounded native xHCI mouse transport
 */

/*
 * Description:
 *   Retain button edges separately from coalesced relative pointer movement.
 */

#ifndef LS2K3000_MOUSE_POINTER_H
#define LS2K3000_MOUSE_POINTER_H

#include <stdint.h>

#define LS_MOUSE_EDGES 16U

struct ls_mouse_point
{
    uint16_t x;
    uint16_t y;
    uint8_t pressed;
};

struct ls_mouse_pointer
{
    struct ls_mouse_point current;
    struct ls_mouse_point edges[LS_MOUSE_EDGES];
    uint16_t width;
    uint16_t height;
    unsigned int head;
    unsigned int count;
    uint64_t overflows;
};

/* Caller provides synchronization. Motion coalesces; button edges retain order. */
int ls_mouse_pointer_init(struct ls_mouse_pointer *pointer, unsigned int width,
                           unsigned int height);
void ls_mouse_pointer_update(struct ls_mouse_pointer *pointer, int32_t dx,
                              int32_t dy, int pressed);
void ls_mouse_pointer_release(struct ls_mouse_pointer *pointer);
struct ls_mouse_point ls_mouse_pointer_read(struct ls_mouse_pointer *pointer);

#endif
