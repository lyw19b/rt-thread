/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    retain bounded TCP stream stress endpoint
 *
 * Description:
 *   Echo one TCP stream on port 9001 with a fixed heap buffer, partial-send
 *   handling and a wall-clock deadline. The host verifies bytes and ordering.
 */

#include <rtthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static int gmac_tcp_echo(int argc, char **argv)
{
    struct sockaddr_in address = {0};
    struct timeval timeout = {1, 0};
    struct pollfd wait = {0};
    rt_tick_t start = rt_tick_get();
    int seconds = argc > 1 ? atoi(argv[1]) : 120;
    int listener = -1, peer = -1, result = -RT_ERROR;
    unsigned char *buffer = RT_NULL;
    rt_uint64_t bytes = 0;

    if (seconds < 1 || seconds > 600)
    {
        return -RT_EINVAL;
    }

    buffer = rt_malloc(8192);
    listener = socket(AF_INET, SOCK_STREAM, 0);

    if (!buffer || listener < 0)
    {
        goto out;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(9001);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1))
    {
        goto out;
    }

    rt_kprintf("GMAC TCP: port 9001, deadline=%d seconds\n", seconds);
    wait.fd = listener;
    wait.events = POLLIN;

    while (rt_tick_get() - start < (rt_tick_t)seconds * RT_TICK_PER_SECOND)
    {
        int ready = poll(&wait, 1, 1000);

        if (ready < 0 && errno != EINTR)
        {
            goto out;
        }

        if (ready > 0 && (wait.revents & POLLIN))
        {
            peer = accept(listener, RT_NULL, RT_NULL);
            break;
        }
    }

    if (peer < 0 ||
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
    {
        goto out;
    }

    while (rt_tick_get() - start < (rt_tick_t)seconds * RT_TICK_PER_SECOND)
    {
        int count = recv(peer, buffer, 8192, 0);
        int sent = 0;

        if (count == 0)
        {
            result = RT_EOK;
            break;
        }

        if (count < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }

            goto out;
        }

        while (sent < count)
        {
            int written;

            if (rt_tick_get() - start >= (rt_tick_t)seconds * RT_TICK_PER_SECOND)
            {
                goto out;
            }

            written = send(peer, buffer + sent, count - sent, 0);

            if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }

            if (written <= 0)
            {
                goto out;
            }

            sent += written;
            bytes += written;
        }
    }

out:
    if (peer >= 0)
    {
        close(peer);
    }

    if (listener >= 0)
    {
        close(listener);
    }

    rt_free(buffer);
    rt_kprintf("GMAC TCP: bytes=%llu result=%d\n", bytes, result);
    return result;
}
MSH_CMD_EXPORT(gmac_tcp_echo, Echo one bounded TCP stream for payload validation);
