/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: display_test.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Draw startup status, color bars and border checks without a GUI dependency.
 */

#include "display_test.h"

/* Five-column, seven-row glyphs. Scaling is identical on both axes. */
static const struct
{
    char character;
    uint8_t rows[7];
} glyphs[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 25, 21, 19, 19, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 27, 17}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {':', {0, 4, 4, 0, 4, 4, 0}},
};

static uint32_t pixel_color(const struct ls2k3000_surface *surface, uint32_t rgb)
{
    if (surface->format == 0)
    {
        return ((rgb & 255) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 255);
    }

    return rgb;
}

static void rectangle(const struct ls2k3000_surface *surface,
                       unsigned int x, unsigned int y, unsigned int width,
                       unsigned int height, uint32_t color)
{
    unsigned int end_x;
    unsigned int end_y;

    if (x >= surface->width || y >= surface->height)
    {
        return;
    }

    end_x = width > surface->width - x ? surface->width : x + width;
    end_y = height > surface->height - y ? surface->height : y + height;
    color = pixel_color(surface, color);

    for (unsigned int row = y; row < end_y; ++row)
    {
        volatile uint32_t *pixels = surface->pixels + (size_t)row * surface->stride;

        for (unsigned int column = x; column < end_x; ++column)
        {
            pixels[column] = color;
        }
    }
}

static int glyph_pixel(const uint8_t *rows, int column, int row)
{
    if (column < 0 || column >= 5 || row < 0 || row >= 7)
    {
        return 0;
    }

    return (rows[row] & (1U << (4 - column))) != 0;
}

static void glyph_stroke(const struct ls2k3000_surface *surface, unsigned int x,
                          unsigned int y, int dx, int dy, unsigned int length,
                          unsigned int thickness, uint32_t color)
{
    for (unsigned int step = 0; step <= length; ++step)
    {
        rectangle(surface, (unsigned int)((int)x + dx * (int)step),
                  (unsigned int)((int)y + dy * (int)step),
                  thickness, thickness, color);
    }
}

static unsigned int text(const struct ls2k3000_surface *surface, unsigned int x,
                          unsigned int y, const char *message, unsigned int scale,
                          uint32_t color)
{
    unsigned int thickness = (scale + 2) / 3;
    unsigned int inset = (scale - thickness) / 2;

    for (; *message; ++message, x += 6 * scale)
    {
        for (unsigned int glyph = 0; glyph < sizeof(glyphs) / sizeof(glyphs[0]); ++glyph)
        {
            const uint8_t *rows = glyphs[glyph].rows;

            if (glyphs[glyph].character != *message)
            {
                continue;
            }

            for (int row = 0; row < 7; ++row)
            {
                for (int column = 0; column < 5; ++column)
                {
                    unsigned int px = x + (unsigned int)column * scale + inset;
                    unsigned int py = y + (unsigned int)row * scale + inset;

                    if (!glyph_pixel(rows, column, row))
                    {
                        continue;
                    }

                    /* Thin connected strokes preserve the original glyph proportions. */
                    rectangle(surface, px, py, thickness, thickness, color);

                    if (glyph_pixel(rows, column + 1, row))
                    {
                        rectangle(surface, px, py, scale + thickness, thickness, color);
                    }

                    if (glyph_pixel(rows, column, row + 1))
                    {
                        rectangle(surface, px, py, thickness, scale + thickness, color);
                    }
                    else
                    {
                        for (int direction = -1; direction <= 1; direction += 2)
                        {
                            if (!glyph_pixel(rows, column + direction, row) &&
                                glyph_pixel(rows, column + direction, row + 1))
                            {
                                glyph_stroke(surface, px, py, direction, 1, scale,
                                              thickness, color);
                            }
                        }
                    }
                }
            }

            break;
        }
    }

    return x;
}

static unsigned int number(const struct ls2k3000_surface *surface, unsigned int x,
                            unsigned int y, unsigned int value, unsigned int scale)
{
    char digits[11];
    unsigned int length = 0;

    do
    {
        digits[length++] = '0' + value % 10;
        value /= 10;
    } while (value);

    while (length)
    {
        char digit[2] = {digits[--length], 0};

        x = text(surface, x, y, digit, scale, 0xe5eef8);
    }

    return x;
}

int ls2k3000_display_draw(const struct ls2k3000_surface *surface,
                         unsigned int cpus, int smart)
{
    static const uint32_t colors[] = {
        0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
        0xff00ff, 0xff0000, 0x0000ff, 0x000000,
    };
    unsigned int scale;
    unsigned int margin;
    unsigned int x;
    unsigned int bar_y;
    unsigned int bar_width;

    if (!surface || !surface->pixels || ((uintptr_t)surface->pixels & 3) ||
        surface->width < 320 || surface->height < 200 || surface->width > 8192 ||
        surface->height > 8192 || surface->stride < surface->width ||
        surface->stride > 16384 || surface->format > 1 || !cpus || cpus > 8 ||
        (uint64_t)surface->stride * surface->height * 4 > surface->bytes)
    {
        return -1;
    }

    scale = surface->width / 320;

    if (scale > surface->height / 200)
    {
        scale = surface->height / 200;
    }

    if (scale > 6)
    {
        scale = 6;
    }

    margin = 16 * scale;
    rectangle(surface, 0, 0, surface->width, surface->height, 0x101a2b);
    rectangle(surface, 0, 0, surface->width, 2, 0x68baff);
    rectangle(surface, 0, surface->height - 2, surface->width, 2, 0x68baff);
    rectangle(surface, 0, 0, 2, surface->height, 0x68baff);
    rectangle(surface, surface->width - 2, 0, 2, surface->height, 0x68baff);
    rectangle(surface, margin, margin, 4 * scale, 28 * scale, 0x39d6b0);
    text(surface, margin + 12 * scale, margin, "LOONGSON LS2K3000", scale, 0x91abc5);
    text(surface, margin + 12 * scale, margin + 14 * scale,
         smart ? "RT-THREAD SMART" : "RT-THREAD", 2 * scale, 0xffffff);
    text(surface, margin, margin + 48 * scale, "KERNEL ONLINE", scale, 0x39d6b0);
    x = text(surface, margin, margin + 64 * scale, "CPU CORES: ", scale, 0x91abc5);
    number(surface, x, margin + 64 * scale, cpus, scale);
    x = text(surface, margin, margin + 80 * scale, "DISPLAY: ", scale, 0x91abc5);
    x = number(surface, x, margin + 80 * scale, surface->width, scale);
    x = text(surface, x, margin + 80 * scale, " X ", scale, 0x91abc5);
    number(surface, x, margin + 80 * scale, surface->height, scale);
    bar_y = surface->height - margin - 50 * scale;
    bar_width = surface->width - 2 * margin;

    for (unsigned int index = 0; index < 8; ++index)
    {
        unsigned int first = index * bar_width / 8;
        unsigned int last = (index + 1) * bar_width / 8;

        rectangle(surface, margin + first, bar_y, last - first, 28 * scale, colors[index]);
    }

    text(surface, margin, bar_y + 36 * scale, "LEFT", scale, 0xe5eef8);
    text(surface, surface->width - margin - 30 * scale, bar_y + 36 * scale,
         "RIGHT", scale, 0xe5eef8);
    return 0;
}
