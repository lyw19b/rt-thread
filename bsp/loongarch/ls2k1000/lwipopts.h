/*
 * Copyright (c) 2026, OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                  Notes
 * 2026-10-26     OpenLANPU Contributors    size Xingyun UDP burst queues
 *
 * Description:
 *   Reuse the RT-Thread port configuration and override only the board's
 *   UDP queue, byte budget and backing pools. The BSP include path precedes
 *   the port path.
 */

#ifndef LS2K1000_LWIPOPTS_H
#define LS2K1000_LWIPOPTS_H

#include "../../../components/net/lwip/port/lwipopts.h"

/* A single-packet mailbox drops a burst while the application is scheduled
 * behind the TCP/IP worker. Keep this bounded independently of the DMA ring. */
#undef DEFAULT_UDP_RECVMBOX_SIZE
#define DEFAULT_UDP_RECVMBOX_SIZE 64

/* Keep the backing pools large enough for the configured mailbox depths. */
#undef MEMP_NUM_NETBUF
#define MEMP_NUM_NETBUF 128

#undef MEMP_NUM_TCPIP_MSG_INPKT
#define MEMP_NUM_TCPIP_MSG_INPKT 128

/* The byte budget must also hold a full 64-packet, MTU-sized UDP burst. */
#undef RECV_BUFSIZE_DEFAULT
#define RECV_BUFSIZE_DEFAULT (128 * 1024)

#endif
