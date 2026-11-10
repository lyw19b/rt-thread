/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    define private virtual-display interface
 */
#ifndef LS2K_VNC_DISPLAY_H
#define LS2K_VNC_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifndef LCD_WIDTH
#define LCD_WIDTH 1024
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 600
#endif
#define VNC_FRAME_BYTES ((size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(uint32_t))

/* Kernel-only device control. The checked UI service owns the packed source;
 * no network client or user process receives this pointer.
 */
#define VNC_DISPLAY_COMMIT_REGION 0x564e01
struct vnc_display_region
{
    const uint32_t *pixels;
    uint32_t x, y, width, height;
};

int vnc_display_ready(void);
/* A coherent copy; no network operations are permitted while its lock is held. */
void vnc_display_snapshot(uint32_t *pixels, uint64_t *sequence);
uint64_t vnc_display_sequence(void);
void vnc_display_pointer(uint16_t x, uint16_t y, int pressed);
/* Drop queued old input and deliver a release on disconnect/reconnect. */
void vnc_display_release_pointer(void);

#endif
