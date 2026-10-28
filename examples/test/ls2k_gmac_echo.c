/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtsched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/* Bounded diagnostic server, enabled only by BSP_GMAC_USING_ECHO_TEST. */
static int gmac_echo(int argc, char **argv)
{
    struct sockaddr_in local = {0}, peer;
    struct timeval timeout = {1, 0};
    unsigned char buffer[1472];
    rt_tick_t start;
    unsigned int packets = 0, bytes = 0;
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    int priority = argc > 2 ? atoi(argv[2]) : RT_THREAD_PRIORITY_MAX - 2;
    rt_thread_t thread = rt_thread_self();
    rt_uint8_t saved_priority = RT_SCHED_PRIV(thread).init_priority;
    rt_uint8_t echo_priority;
    rt_bool_t priority_changed = RT_FALSE;
    int fd;
    int result = RT_EOK;

    if (argc > 3 || seconds < 1 || seconds > 3600 ||
        priority < 1 || priority >= RT_THREAD_PRIORITY_MAX - 1)
    {
        rt_kprintf("usage: gmac_echo [seconds 1..3600] [thread priority]\n");
        return -RT_EINVAL;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return -RT_ERROR;
    }

    local.sin_family = AF_INET;
    local.sin_port = htons(9000);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0)
    {
        result = -RT_ERROR;
        goto out;
    }

    /* A continuously backlogged FinSH command otherwise keeps its priority
     * above the UI and converter for the whole run. Use background priority
     * for coexistence tests; an explicit priority retains contention tests. */
    echo_priority = priority;
    result = rt_thread_control(thread, RT_THREAD_CTRL_RESET_PRIORITY, &echo_priority);

    if (result != RT_EOK)
    {
        goto out;
    }

    priority_changed = RT_TRUE;
    rt_kprintf("GMAC echo: UDP port 9000, %d seconds, priority=%u (shell=%u)\n",
               seconds, echo_priority, saved_priority);
    start = rt_tick_get();

    while (rt_tick_get() - start < (rt_tick_t)seconds * RT_TICK_PER_SECOND)
    {
        socklen_t peer_size = sizeof(peer);
        int count = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&peer, &peer_size);

        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                continue;
            }

            result = -RT_ERROR;
            break;
        }

        if (sendto(fd, buffer, count, 0, (struct sockaddr *)&peer, peer_size) != count)
        {
            result = -RT_ERROR;
            break;
        }

        packets++;
        bytes += count;
    }

    rt_kprintf("GMAC echo: packets=%u bytes=%u result=%d\n", packets, bytes, result);

out:
    close(fd);

    if (priority_changed)
    {
        rt_thread_control(thread, RT_THREAD_CTRL_RESET_PRIORITY, &saved_priority);
    }

    return result;
}

MSH_CMD_EXPORT(gmac_echo, Echo UDP packets for a bounded hardware test);
