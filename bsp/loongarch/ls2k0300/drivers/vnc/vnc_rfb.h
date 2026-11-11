/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    add bounded, transport-independent RFB
 */
#ifndef LS2K_VNC_RFB_H
#define LS2K_VNC_RFB_H

#include <stddef.h>
#include <stdint.h>

/* Transport callbacks may return short transfers; zero/error ends the session.
 * snapshot copies an immutable ARGB8888 frame and its sequence number.
 * All callbacks run on the network thread, never in an interrupt or UI callback.
 */
struct vnc_rfb_io
{
    void *context;
    int (*receive)(void *context, void *data, size_t size);
    int (*send)(void *context, const void *data, size_t size);
    void (*snapshot)(void *context, uint32_t *pixels, uint64_t *sequence);
    void (*pointer)(void *context, uint16_t x, uint16_t y, int pressed);
    uint64_t (*sequence)(void *context);
};

struct vnc_rfb_rectangle
{
    uint16_t x, y, width, height;
};

struct vnc_rfb_session
{
    struct vnc_rfb_io io;
    uint16_t width, height;
    /* Caller owns these buffers for the entire session: width*height pixels,
     * and at least width*4 row bytes. Larger row storage batches cropped Raw
     * scanlines into fewer transport calls. The core performs no allocations.
     */
    uint32_t *pixels;
    /* Optional last-delivered image, also width*height pixels. Enables dirty
     * rectangle updates. Never replace it with the UI's writable framebuffer.
     */
    uint32_t *previous;
    uint8_t *row;
    size_t row_bytes;
    uint8_t format[16];
    struct vnc_rfb_rectangle requested;
    uint64_t delivered_sequence;
    int pending, incremental, delivered_full_frame;
    int hextile;
    int local_cursor, cursor_sent;
};

int vnc_rfb_handshake(struct vnc_rfb_session *session);
int vnc_rfb_receive_message(struct vnc_rfb_session *session);
/* Call periodically even when no input is arriving, to satisfy a pending
 * incremental request after the UI changes. Returns -1 on transport failure.
 */
int vnc_rfb_send_update(struct vnc_rfb_session *session);

#endif
