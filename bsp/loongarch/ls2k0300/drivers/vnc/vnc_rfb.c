/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    implement RFB 3.3/3.7/3.8 and Raw updates
 */

/* Wire messages follow RFC 6143. No packed structs, unaligned loads, or host
 * byte-order assumptions cross the network boundary. Unsupported extensions
 * close only this client; framebuffer and UI ownership are unaffected.
 */
#include "vnc_rfb.h"
#include <string.h>

#define VNC_MAX_ENCODINGS 256U
#define VNC_MAX_CUT_TEXT  65536U

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((unsigned)data[0] << 8 | data[1]);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = value >> 8;
    data[1] = value;
}

static int receive_exact(struct vnc_rfb_session *session, void *data, size_t size)
{
    uint8_t *target = data;

    while (size)
    {
        int count = session->io.receive(session->io.context, target, size);

        if (count <= 0 || (size_t)count > size)
        {
            return -1;
        }
        target += count;
        size -= count;
    }
    return 0;
}

static int send_exact(struct vnc_rfb_session *session, const void *data, size_t size)
{
    const uint8_t *source = data;

    while (size)
    {
        int count = session->io.send(session->io.context, source, size);

        if (count <= 0 || (size_t)count > size)
        {
            return -1;
        }
        source += count;
        size -= count;
    }
    return 0;
}

static int discard_bytes(struct vnc_rfb_session *session, uint32_t size)
{
    uint8_t scratch[128];

    while (size)
    {
        size_t chunk = size < sizeof(scratch) ? size : sizeof(scratch);

        if (receive_exact(session, scratch, chunk))
        {
            return -1;
        }
        size -= chunk;
    }
    return 0;
}

static int valid_format(const uint8_t *format)
{
    uint32_t occupied = 0;
    unsigned depth = 0;
    unsigned channel;

    if ((format[0] != 8 && format[0] != 16 && format[0] != 32) ||
        !format[1] || format[1] > format[0] || format[2] > 1 || format[3] != 1)
    {
        return 0;
    }

    for (channel = 0; channel < 3; channel++)
    {
        uint32_t maximum = read_u16(format + 4 + channel * 2);
        unsigned shift = format[10 + channel];
        unsigned bits = 0;
        uint32_t value = maximum;

        /* Each maximum must be 2^N-1. Check before shifts to avoid UB. */
        if (!maximum || (maximum & (maximum + 1)))
        {
            return 0;
        }
        while (value)
        {
            bits++;
            value >>= 1;
        }
        if (shift >= format[0] || bits > format[0] - shift ||
            (occupied & (maximum << shift)))
        {
            return 0;
        }
        occupied |= maximum << shift;
        depth += bits;
    }
    return depth <= format[1];
}

int vnc_rfb_handshake(struct vnc_rfb_session *session)
{
    static const char version[] = "RFB 003.008\n";
    static const char name[] = "NovaUIOS / RT-Thread";
    static const uint8_t default_format[16] = {
        32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0
    };
    uint8_t input[12], init[24] = {0};
    uint8_t security[] = {1, 1}; /* One security type: None. */
    uint8_t success[4] = {0};
    int minor;

    if (!session->width || !session->height || !session->pixels || !session->row ||
        send_exact(session, version, 12) || receive_exact(session, input, 12))
    {
        return -1;
    }
    if (!memcmp(input, "RFB 003.003\n", 12))
    {
        minor = 3;
    }
    else if (!memcmp(input, "RFB 003.007\n", 12))
    {
        minor = 7;
    }
    else if (!memcmp(input, version, 12))
    {
        minor = 8;
    }
    else
    {
        return -1;
    }

    if (minor == 3)
    {
        uint8_t none[4] = {0, 0, 0, 1};

        if (send_exact(session, none, sizeof(none)))
        {
            return -1;
        }
    }
    else if (send_exact(session, security, sizeof(security)) ||
             receive_exact(session, input, 1) || input[0] != 1 ||
             (minor == 8 && send_exact(session, success, sizeof(success))))
    {
        return -1;
    }
    if (receive_exact(session, input, 1)) /* ClientInit shared flag */
    {
        return -1;
    }

    memcpy(session->format, default_format, sizeof(default_format));
    session->pending = 0;
    session->delivered_full_frame = 0;
    write_u16(init, session->width);
    write_u16(init + 2, session->height);
    memcpy(init + 4, default_format, 16);
    init[23] = sizeof(name) - 1;
    return send_exact(session, init, sizeof(init)) ||
           send_exact(session, name, sizeof(name) - 1) ? -1 : 0;
}

static int request_update(struct vnc_rfb_session *session, const uint8_t *message)
{
    struct vnc_rfb_rectangle request;
    unsigned right, bottom;

    request.x = read_u16(message + 1);
    request.y = read_u16(message + 3);
    request.width = read_u16(message + 5);
    request.height = read_u16(message + 7);
    if (request.x >= session->width || request.y >= session->height ||
        !request.width || !request.height)
    {
        return -1;
    }
    if (request.width > session->width - request.x)
    {
        request.width = session->width - request.x;
    }
    if (request.height > session->height - request.y)
    {
        request.height = session->height - request.y;
    }

    /* Multiple outstanding requests may be satisfied by one combined update.
     * Any nonincremental request forces a response, even on an unchanged UI.
     */
    if (session->pending)
    {
        right = request.x + request.width;
        bottom = request.y + request.height;
        if (right < session->requested.x + session->requested.width)
            right = session->requested.x + session->requested.width;
        if (bottom < session->requested.y + session->requested.height)
            bottom = session->requested.y + session->requested.height;
        if (request.x > session->requested.x)
            request.x = session->requested.x;
        if (request.y > session->requested.y)
            request.y = session->requested.y;
        request.width = right - request.x;
        request.height = bottom - request.y;
        session->incremental = session->incremental && message[0] != 0;
    }
    else
    {
        session->incremental = message[0] != 0;
    }
    session->requested = request;
    session->pending = 1;
    return 0;
}

int vnc_rfb_receive_message(struct vnc_rfb_session *session)
{
    uint8_t type, message[19];

    if (receive_exact(session, &type, 1))
    {
        return -1;
    }
    switch (type)
    {
    case 0: /* SetPixelFormat */
        if (receive_exact(session, message, 19) || !valid_format(message + 3))
            return -1;
        memcpy(session->format, message + 3, 16);
        session->delivered_full_frame = 0;
        session->cursor_sent = 0;
        return 0;

    case 2: /* SetEncodings: Raw is always available, even when omitted. */
    {
        unsigned count, index;

        if (receive_exact(session, message, 3) || read_u16(message + 1) > VNC_MAX_ENCODINGS)
            return -1;
        count = read_u16(message + 1);
        session->hextile = 0;
        session->local_cursor = 0;
        session->cursor_sent = 0;
        for (index = 0; index < count; index++)
        {
            if (receive_exact(session, message, 4))
                return -1;
            if (read_u32(message) == 5)
                session->hextile = 1;
            if (read_u32(message) == 0xffffff11U) /* Cursor pseudo-encoding -239 */
                session->local_cursor = 1;
        }
        return 0;
    }

    case 3: /* FramebufferUpdateRequest */
        if (receive_exact(session, message, 9))
            return -1;
        return request_update(session, message);

    case 4: /* KeyEvent: this UI uses pointer input only. */
        return discard_bytes(session, 7);

    case 5: /* PointerEvent */
        if (receive_exact(session, message, 5))
            return -1;
        session->io.pointer(session->io.context,
            read_u16(message + 1) < session->width ? read_u16(message + 1) : session->width - 1,
            read_u16(message + 3) < session->height ? read_u16(message + 3) : session->height - 1,
            (message[0] & 1) != 0);
        return 0;

    case 6: /* ClientCutText: bounded discard, no clipboard allocation. */
        if (receive_exact(session, message, 7) || read_u32(message + 3) > VNC_MAX_CUT_TEXT)
            return -1;
        return discard_bytes(session, read_u32(message + 3));

    default:
        return -1;
    }
}

static int native_pixel_format(const struct vnc_rfb_session *session)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* ARGB8888 already has the default RFB byte layout. The unused high byte
     * is padding, so sending it unchanged needs no per-pixel arithmetic.
     */
    static const uint8_t native_format[13] = {
        32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0
    };
    return !memcmp(session->format, native_format, sizeof(native_format));
#else
    (void)session;
    return 0;
#endif
}

static void encode_row(struct vnc_rfb_session *session, const uint32_t *source, unsigned width)
{
    unsigned bytes = session->format[0] / 8;
    unsigned x, channel, byte;

    if (native_pixel_format(session))
    {
        memcpy(session->row, source, (size_t)width * 4);
        return;
    }

    for (x = 0; x < width; x++)
    {
        uint32_t pixel = 0;

        for (channel = 0; channel < 3; channel++)
        {
            unsigned component = (source[x] >> (16 - channel * 8)) & 255;
            uint32_t maximum = read_u16(session->format + 4 + channel * 2);

            pixel |= ((component * maximum + 127) / 255) << session->format[10 + channel];
        }
        for (byte = 0; byte < bytes; byte++)
        {
            unsigned shift = session->format[2] ? (bytes - byte - 1) * 8 : byte * 8;

            session->row[x * bytes + byte] = pixel >> shift;
        }
    }
}

/* Hextile divides a rectangle into at most 16x16 tiles. Each tile uses its
 * first pixel as background and represents other horizontal runs as colored
 * subrectangles. Dense tiles fall back to Raw, so expansion is at most one byte
 * per tile. Every encoded tile supplies its background: no inherited color
 * state can be invalidated by a preceding Raw tile or format negotiation.
 */
static int send_hextile(struct vnc_rfb_session *session, const struct vnc_rfb_rectangle *area)
{
    uint8_t tile[1 + 4 + 1 + 255 * 6];
    uint8_t batch[4096];
    size_t buffered = 0;
    unsigned bytes = session->format[0] / 8;
    unsigned tile_x, tile_y;

    for (tile_y = 0; tile_y < area->height; tile_y += 16)
    {
        for (tile_x = 0; tile_x < area->width; tile_x += 16)
        {
            unsigned width = area->width - tile_x, height = area->height - tile_y;
            const uint32_t *origin = session->pixels +
                (size_t)(area->y + tile_y) * session->width + area->x + tile_x;
            uint32_t background = origin[0] & 0xffffffU;
            unsigned x, y, runs = 0;
            size_t used = 2 + bytes, raw_size;
            int raw = 0;

            if (width > 16) width = 16;
            if (height > 16) height = 16;
            raw_size = 1 + width * height * bytes;
            encode_row(session, &background, 1);
            memcpy(tile + 1, session->row, bytes);

            for (y = 0; y < height && !raw; y++)
            {
                const uint32_t *source = origin + (size_t)y * session->width;

                for (x = 0; x < width;)
                {
                    uint32_t color = source[x] & 0xffffffU;
                    unsigned start = x++;

                    if (color == background)
                        continue;
                    while (x < width && (source[x] & 0xffffffU) == color)
                        x++;
                    if (runs == 255 || used + bytes + 2 >= raw_size)
                    {
                        raw = 1;
                        break;
                    }
                    encode_row(session, &color, 1);
                    memcpy(tile + used, session->row, bytes);
                    used += bytes;
                    tile[used++] = (start << 4) | y;
                    tile[used++] = (x - start - 1) << 4; /* height = 1 */
                    runs++;
                }
            }
            if (raw || used >= raw_size)
            {
                tile[0] = 1;
                used = 1;
                for (y = 0; y < height; y++)
                {
                    encode_row(session, origin + (size_t)y * session->width, width);
                    memcpy(tile + used, session->row, width * bytes);
                    used += width * bytes;
                }
            }
            else
            {
                tile[0] = runs ? 26 : 2; /* BackgroundSpecified, AnySubrects, Colored */
                tile[1 + bytes] = runs;
                if (!runs)
                    used = 1 + bytes;
            }
            if (buffered + used > sizeof(batch))
            {
                if (send_exact(session, batch, buffered))
                    return -1;
                buffered = 0;
            }
            memcpy(batch + buffered, tile, used);
            buffered += used;
        }
    }
    return send_exact(session, batch, buffered);
}

/* The cursor is a viewer-side shape, not part of the framebuffer. The mask
 * preserves transparent pixels; coordinates in this rectangle are its hotspot.
 */
static int send_cursor(struct vnc_rfb_session *session)
{
    static const char *shape[] = {
        "X...........", "XX..........", "XOX.........", "XOOX........",
        "XOOOX.......", "XOOOOX......", "XOOOOOX.....", "XOOOOOOX....",
        "XOOOOOOOX...", "XOOOOOOOOX..", "XOOOOXXXXX..", "XOOXOX......",
        "XOX.OX......", "XX..XOX.....", "X...XOX.....", ".....XOX....",
        ".....XOX....", "......X.....",
    };
    uint8_t header[12] = {0}, mask[18 * 2] = {0};
    uint8_t encoded[18 * 12 * 4 + sizeof(mask)];
    unsigned bytes = session->format[0] / 8;
    uint32_t pixels[12];
    unsigned x, y;

    write_u16(header + 4, 12);
    write_u16(header + 6, 18);
    header[8] = header[9] = header[10] = 255;
    header[11] = 17; /* -239 */
    if (send_exact(session, header, sizeof(header)))
        return -1;
    for (y = 0; y < 18; y++)
    {
        for (x = 0; x < 12; x++)
        {
            pixels[x] = shape[y][x] == 'O' ? 0xffffffU : 0;
            if (shape[y][x] != '.')
                mask[y * 2 + x / 8] |= 0x80U >> (x % 8);
        }
        /* row storage belongs to the display width. Small test geometries
         * may be narrower than the cursor; encode one pixel at a time.
         */
        for (x = 0; x < 12; x++)
        {
            encode_row(session, pixels + x, 1);
            memcpy(encoded + (y * 12 + x) * bytes, session->row, bytes);
        }
    }
    memcpy(encoded + 18 * 12 * bytes, mask, sizeof(mask));
    return send_exact(session, encoded, 18 * 12 * bytes + sizeof(mask));
}

static int send_rectangle(struct vnc_rfb_session *session, const struct vnc_rfb_rectangle *area)
{
    uint8_t header[12] = {0};
    unsigned y;

    write_u16(header, area->x);
    write_u16(header + 2, area->y);
    write_u16(header + 4, area->width);
    write_u16(header + 6, area->height);
    header[11] = session->hextile ? 5 : 0;
    if (send_exact(session, header, sizeof(header)))
        return -1;
    if (session->hextile)
    {
        if (send_hextile(session, area))
            return -1;
    }
    else if (!area->x && area->width == session->width && native_pixel_format(session))
    {
        if (send_exact(session, session->pixels + (size_t)area->y * session->width,
                       (size_t)area->height * session->width * 4))
            return -1;
    }
    else
    {
        for (y = area->y; y < (unsigned)area->y + area->height; y++)
        {
            encode_row(session, session->pixels + (size_t)y * session->width + area->x, area->width);
            if (send_exact(session, session->row, (size_t)area->width * (session->format[0] / 8)))
                return -1;
        }
    }
    if (session->previous)
    {
        for (y = area->y; y < (unsigned)area->y + area->height; y++)
        {
            size_t offset = (size_t)y * session->width + area->x;

            memcpy(session->previous + offset, session->pixels + offset, (size_t)area->width * 4);
        }
    }
    return 0;
}

static int tile_changed(const struct vnc_rfb_session *session,
                        unsigned left, unsigned top, unsigned width, unsigned height)
{
    unsigned x, y;

    for (y = top; y < top + height; y++)
    {
        size_t offset = (size_t)y * session->width + left;

        for (x = 0; x < width; x++)
        {
            if ((session->pixels[offset + x] ^ session->previous[offset + x]) & 0xffffffU)
                return 1;
        }
    }
    return 0;
}

/* Two passes over the same immutable snapshot: first count, then send. Adjacent
 * dirty 32x32 tiles merge along each row; distant changes stay separate, so a
 * small pointer/button change never pulls an unrelated screen area into it.
 * No rectangle list, per-frame allocations, or unbounded stack arrays are used.
 */
static int visit_dirty_rectangles(struct vnc_rfb_session *session, int transmit)
{
    const struct vnc_rfb_rectangle *request = &session->requested;
    unsigned bottom = request->y + request->height, right = request->x + request->width;
    unsigned x, y;
    int count = 0;

    for (y = request->y; y < bottom; y += 32)
    {
        unsigned height = bottom - y < 32 ? bottom - y : 32;
        unsigned run = right;

        for (x = request->x; x <= right;)
        {
            unsigned width = right - x < 32 ? right - x : 32;
            int changed = x < right && tile_changed(session, x, y, width, height);

            if (changed && run == right)
                run = x;
            if (!changed && run != right)
            {
                struct vnc_rfb_rectangle area = {run, y, x - run, height};

                if (transmit && send_rectangle(session, &area))
                    return -1;
                count++;
                run = right;
            }
            if (x == right)
                break;
            x += width;
        }
    }
    return count;
}

int vnc_rfb_send_update(struct vnc_rfb_session *session)
{
    uint8_t header[4] = {0};
    uint64_t sequence;
    int rectangles, dirty, cursor;
    int full_request;

    if (!session->pending)
        return 0;
    cursor = session->local_cursor && !session->cursor_sent;
    if (!cursor && session->incremental && session->delivered_full_frame &&
        session->io.sequence && session->io.sequence(session->io.context) == session->delivered_sequence)
        return 0;
    session->io.snapshot(session->io.context, session->pixels, &sequence);
    if (!cursor && session->incremental && session->delivered_full_frame &&
        sequence == session->delivered_sequence)
        return 0;
    dirty = session->incremental && session->previous && session->delivered_full_frame;
    rectangles = dirty ? visit_dirty_rectangles(session, 0) : 1;
    full_request = !session->requested.x && !session->requested.y &&
        session->requested.width == session->width && session->requested.height == session->height;
    if (!rectangles && !cursor)
    {
        if (full_request)
            session->delivered_sequence = sequence;
        return 0;
    }
    if (rectangles + cursor > 65535)
        return -1;
    write_u16(header + 2, rectangles + cursor);
    if (send_exact(session, header, sizeof(header)))
        return -1;
    if (cursor && send_cursor(session))
        return -1;
    if (dirty)
    {
        if (visit_dirty_rectangles(session, 1) < 0)
            return -1;
    }
    else if (send_rectangle(session, &session->requested))
    {
        return -1;
    }
    if (cursor)
        session->cursor_sent = 1;
    if (!session->previous)
        session->delivered_full_frame = full_request;
    else if (full_request)
        session->delivered_full_frame = 1;
    if (full_request || !session->previous)
        session->delivered_sequence = sequence;
    session->pending = 0;
    return 0;
}
