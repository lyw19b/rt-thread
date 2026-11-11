/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    serve one remote viewer on the BSP network
 */

/* Only this thread owns the sockets and session buffers. The display path never
 * waits for network I/O. A stalled or malformed client is closed; the listener
 * remains available for reconnect. No filesystem or UI calls run in this thread.
 */
#include <rtthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include "vnc_display.h"
#include "vnc_rfb.h"

#define VNC_THREAD_STACK_SIZE 16384
#define VNC_UPDATE_INTERVAL_MS 16
#define VNC_TRANSFER_TIMEOUT_MS 3000
#define VNC_ROW_BATCH_BYTES 32768U

struct vnc_connection
{
    int socket;
    int pump_input;
    struct vnc_rfb_session session;
};

static uint64_t received_bytes, sent_bytes;
static uint32_t pointer_events, pointer_releases, last_pointer_tick;
static uint64_t probe_ticks, send_ticks, pause_ticks, snapshot_ticks, update_ticks;
static uint32_t send_calls, update_calls;

static int socket_receive(void *context, void *data, size_t size)
{
    struct vnc_connection *connection = context;

    int count = recv(connection->socket, data, size, 0);

    if (count > 0)
        __atomic_fetch_add(&received_bytes, count, __ATOMIC_RELAXED);
    return count;
}

static int socket_send(void *context, const void *data, size_t size)
{
    struct vnc_connection *connection = context;
    rt_tick_t start = rt_tick_get();

    if (size > 32768)
        size = 32768;

    /* A large update must never prevent reception of mouse input. The sender
     * uses a frozen session copy; newly received formats/requests affect the
     * next update, not the pixels already being sent on the wire.
     */
    for (;;)
    {
        int result;
        unsigned messages;

        if ((rt_tick_t)(rt_tick_get() - start) > rt_tick_from_millisecond(VNC_TRANSFER_TIMEOUT_MS))
            return -1;
        for (messages = 0; connection->pump_input && messages < 8; messages++)
        {
            uint8_t first;
            rt_tick_t before = rt_tick_get();

            result = recv(connection->socket, &first, 1, MSG_PEEK | MSG_DONTWAIT);
            __atomic_fetch_add(&probe_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (result <= 0 || vnc_rfb_receive_message(&connection->session))
                return -1;
        }
        rt_tick_t before = rt_tick_get();
        result = send(connection->socket, data, size, MSG_DONTWAIT);
        __atomic_fetch_add(&send_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
        __atomic_fetch_add(&send_calls, 1, __ATOMIC_RELAXED);
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            /* Avoid select's large TCP_SNDLOWAT threshold when a smaller
             * amount of space is already usable. Yield to TCP/UI between
             * attempts; input is checked again on the next iteration.
             */
            before = rt_tick_get();
            rt_thread_mdelay(1);
            __atomic_fetch_add(&pause_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
            continue;
        }
        if (result > 0)
            __atomic_fetch_add(&sent_bytes, result, __ATOMIC_RELAXED);
        return result;
    }
}

static void snapshot_frame(void *context, uint32_t *pixels, uint64_t *sequence)
{
    (void)context;
    rt_tick_t before = rt_tick_get();
    vnc_display_snapshot(pixels, sequence);
    __atomic_fetch_add(&snapshot_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
}

static uint64_t current_sequence(void *context)
{
    (void)context;
    return vnc_display_sequence();
}

static void pointer_event(void *context, uint16_t x, uint16_t y, int pressed)
{
    (void)context;
    __atomic_fetch_add(&pointer_events, 1, __ATOMIC_RELAXED);
    if (!pressed)
        __atomic_fetch_add(&pointer_releases, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&last_pointer_tick, rt_tick_get(), __ATOMIC_RELAXED);
    vnc_display_pointer(x, y, pressed);
}

static void serve_client(int socket)
{
    struct vnc_connection connection = {0};
    struct timeval timeout = {VNC_TRANSFER_TIMEOUT_MS / 1000, 0};
    rt_tick_t last_update = rt_tick_get();
    int enabled = 1;

    connection.socket = socket;
    connection.session.width = LCD_WIDTH;
    connection.session.height = LCD_HEIGHT;
    connection.session.pixels = rt_malloc(VNC_FRAME_BYTES);
    connection.session.previous = rt_malloc(VNC_FRAME_BYTES);
    connection.session.row_bytes = VNC_ROW_BATCH_BYTES;
    connection.session.row = rt_malloc(connection.session.row_bytes);
    connection.session.io = (struct vnc_rfb_io){
        .context = &connection, .receive = socket_receive, .send = socket_send,
        .snapshot = snapshot_frame, .pointer = pointer_event,
        .sequence = current_sequence,
    };
    if (!connection.session.pixels || !connection.session.previous || !connection.session.row)
        goto cleanup;

    /* SAL/lwIP uses timeval here (LWIP_SO_SNDRCVTIMEO_NONSTANDARD=0). */
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
        goto cleanup;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    vnc_display_release_pointer();
    if (vnc_rfb_handshake(&connection.session))
        goto cleanup;
    connection.pump_input = 1;
    rt_kprintf("VNC: viewer connected\n");

    for (;;)
    {
        fd_set readable;
        struct timeval poll_timeout = {0, 2000};
        int result;

        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        result = select(socket + 1, &readable, RT_NULL, RT_NULL, &poll_timeout);
        if (result < 0 || (result > 0 && vnc_rfb_receive_message(&connection.session)))
            break;

        if ((rt_tick_t)(rt_tick_get() - last_update) >=
            rt_tick_from_millisecond(VNC_UPDATE_INTERVAL_MS))
        {
            struct vnc_rfb_session update = connection.session;
            rt_tick_t before = rt_tick_get();

            /* Pace starts, not completions: encoding/transmission already
             * consume time. Never add another frame interval after a send.
             */
            last_update = before;
            connection.session.pending = 0;
            if (vnc_rfb_send_update(&update))
                break;
            __atomic_fetch_add(&update_ticks, (rt_tick_t)(rt_tick_get() - before), __ATOMIC_RELAXED);
            __atomic_fetch_add(&update_calls, 1, __ATOMIC_RELAXED);
            if (update.pending)
            {
                /* No bytes were sent, so no receive callback changed session. */
                connection.session.pending = 1;
            }
            if (!rt_memcmp(update.format, connection.session.format, sizeof(update.format)))
            {
                connection.session.delivered_full_frame = update.delivered_full_frame;
                connection.session.delivered_sequence = update.delivered_sequence;
                connection.session.cursor_sent = update.cursor_sent;
            }
        }
    }

cleanup:
    vnc_display_release_pointer();
    rt_free(connection.session.row);
    rt_free(connection.session.pixels);
    rt_free(connection.session.previous);
    closesocket(socket);
    rt_kprintf("VNC: viewer disconnected\n");
}

static void server_entry(void *parameter)
{
    struct sockaddr_in address = {0};
    int listener, enabled = 1;

    (void)parameter;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
    {
        rt_kprintf("VNC: cannot create listening socket\n");
        return;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(BSP_VNC_PORT);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1))
    {
        rt_kprintf("VNC: cannot listen on TCP %u\n", BSP_VNC_PORT);
        closesocket(listener);
        return;
    }
    rt_kprintf("VNC READY port=%u size=%ux%u security=none encodings=raw,hextile cursor=local\n",
               BSP_VNC_PORT, LCD_WIDTH, LCD_HEIGHT);

    for (;;)
    {
        int client = accept(listener, RT_NULL, RT_NULL);

        if (client < 0)
        {
            rt_thread_mdelay(100);
            continue;
        }
        serve_client(client);
    }
}

static int vnc_server_init(void)
{
    rt_thread_t thread;

    if (!vnc_display_ready())
        return -RT_ENOSYS;
    thread = rt_thread_create("vnc", server_entry, RT_NULL,
                              VNC_THREAD_STACK_SIZE, 26, 5);
    if (!thread)
        return -RT_ENOMEM;
    return rt_thread_startup(thread);
}

INIT_APP_EXPORT(vnc_server_init);

#ifdef RT_USING_FINSH
static long vnc_status(void)
{
    extern void vnc_net_heap_report(void);
    rt_kprintf("VNC rx=%llu tx=%llu pointer=%u releases=%u last_input_tick=%u now=%u\n",
        (unsigned long long)__atomic_load_n(&received_bytes, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&sent_bytes, __ATOMIC_RELAXED),
        __atomic_load_n(&pointer_events, __ATOMIC_RELAXED),
        __atomic_load_n(&pointer_releases, __ATOMIC_RELAXED),
        __atomic_load_n(&last_pointer_tick, __ATOMIC_RELAXED), rt_tick_get());
    rt_kprintf("VNC profile ticks probe=%llu send=%llu pause=%llu snapshot=%llu update=%llu "
               "send_calls=%u update_calls=%u hz=%u\n",
        (unsigned long long)__atomic_load_n(&probe_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&send_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&pause_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&snapshot_ticks, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&update_ticks, __ATOMIC_RELAXED),
        __atomic_load_n(&send_calls, __ATOMIC_RELAXED),
        __atomic_load_n(&update_calls, __ATOMIC_RELAXED), RT_TICK_PER_SECOND);
    vnc_net_heap_report();
#ifdef BSP_VNC_PROFILE_TCP
    extern void vnc_tcp_profile_report(void);
    vnc_tcp_profile_report();
#endif
    return 0;
}
MSH_CMD_EXPORT(vnc_status, Show remote display byte and input counters);
#endif
