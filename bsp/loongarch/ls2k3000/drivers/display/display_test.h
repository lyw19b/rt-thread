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
 *   Render a visible kernel startup check into a validated RGB/BGR surface.
 */

#ifndef LS2K3000_DISPLAY_TEST_H
#define LS2K3000_DISPLAY_TEST_H

#include <stddef.h>
#include <stdint.h>

struct ls2k3000_surface
{
    volatile uint32_t *pixels;
    size_t bytes;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
};

int ls2k3000_display_draw(const struct ls2k3000_surface *surface,
                         unsigned int cpus, int smart);

#endif
