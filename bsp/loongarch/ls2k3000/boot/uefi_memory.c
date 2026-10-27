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
 *   Freestanding compiler helpers; no dependency on firmware or kernel libc.
 */

#include <stddef.h>

void *memcpy(void *target, const void *source, size_t bytes)
{
    unsigned char *out = target;
    const unsigned char *in = source;

    for (size_t index = 0; index < bytes; ++index)
    {
        out[index] = in[index];
    }

    return target;
}

void *memset(void *target, int value, size_t bytes)
{
    unsigned char *out = target;

    for (size_t index = 0; index < bytes; ++index)
    {
        out[index] = (unsigned char)value;
    }

    return target;
}
