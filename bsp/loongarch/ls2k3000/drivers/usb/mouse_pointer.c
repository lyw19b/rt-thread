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
 *   Keep short clicks observable and force release after edge-queue overflow.
 */

#include "mouse_pointer.h"
#include <string.h>

int ls_mouse_pointer_init(struct ls_mouse_pointer *pointer, unsigned int width,
                           unsigned int height)
{
    if (!pointer || !width || !height || width > UINT16_MAX || height > UINT16_MAX)
    {
        return -1;
    }

    memset(pointer, 0, sizeof(*pointer));
    pointer->width = width;
    pointer->height = height;
    pointer->current.x = width / 2;
    pointer->current.y = height / 2;
    return 0;
}

void ls_mouse_pointer_update(struct ls_mouse_pointer *pointer, int32_t dx,
                              int32_t dy, int pressed)
{
    int64_t x = (int64_t)pointer->current.x + dx;
    int64_t y = (int64_t)pointer->current.y + dy;
    unsigned int old_pressed = pointer->current.pressed;

    pointer->current.x = x < 0 ? 0 : x >= pointer->width ? pointer->width - 1 : x;
    pointer->current.y = y < 0 ? 0 : y >= pointer->height ? pointer->height - 1 : y;
    pointer->current.pressed = !!pressed;

    if (old_pressed != pointer->current.pressed)
    {
        if (pointer->count == LS_MOUSE_EDGES)
        {
            /* Lost edge history must not leave a consumer stuck holding a button.
             * Return one explicit UP first, then the latest full state. */
            pointer->overflows++;
            pointer->head = 0;
            pointer->count = 1;
            pointer->edges[0] = pointer->current;
            pointer->edges[0].pressed = 0;
        }
        else
        {
            unsigned int tail = (pointer->head + pointer->count) % LS_MOUSE_EDGES;

            pointer->edges[tail] = pointer->current;
            pointer->count++;
        }
    }
}

void ls_mouse_pointer_release(struct ls_mouse_pointer *pointer)
{
    pointer->current.pressed = 0;
    pointer->head = 0;
    pointer->count = 0;
}

struct ls_mouse_point ls_mouse_pointer_read(struct ls_mouse_pointer *pointer)
{
    if (pointer->count)
    {
        struct ls_mouse_point point = pointer->edges[pointer->head];

        pointer->head = (pointer->head + 1) % LS_MOUSE_EDGES;
        pointer->count--;
        return point;
    }

    return pointer->current;
}
