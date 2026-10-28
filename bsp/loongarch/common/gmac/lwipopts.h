/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               size bounded UDP queues for GMAC bursts
 */

/*
 * Description:
 *   Extend the unchanged shared lwIP options with LoongArch board limits.
 *   A single-core receiver cannot drain a one-entry mailbox during an RX burst.
 */

#ifndef LOONGARCH_GMAC_LWIPOPTS_H
#define LOONGARCH_GMAC_LWIPOPTS_H

#include "../../../../components/net/lwip/port/lwipopts.h"

#if defined(BSP_USING_GMAC) || defined(BSP_USING_LS2K3000_GMAC)
#undef DEFAULT_UDP_RECVMBOX_SIZE
#define DEFAULT_UDP_RECVMBOX_SIZE BSP_GMAC_UDP_QUEUE_SIZE

#undef MEMP_NUM_NETBUF
#define MEMP_NUM_NETBUF (2 * BSP_GMAC_UDP_QUEUE_SIZE)

#undef RECV_BUFSIZE_DEFAULT
#define RECV_BUFSIZE_DEFAULT (BSP_GMAC_UDP_QUEUE_SIZE * 1472)
#endif

#if defined(BSP_USING_LS2K3000_GMAC)
/* tcpip_inpkt allocates a message before posting to TCPIP_MBOX_SIZE. Match
 * that mailbox's bounded capacity; its upstream default pool holds only eight
 * messages and can reject a burst even when the mailbox still has free slots. */
#undef MEMP_NUM_TCPIP_MSG_INPKT
#define MEMP_NUM_TCPIP_MSG_INPKT RT_LWIP_TCPTHREAD_MBOX_SIZE

/* A cold neighbour can queue replies before its ARP response reaches tcpip.
 * Keep one bounded socket burst per neighbour; reserve space for both ports. */
#undef ARP_QUEUE_LEN
#define ARP_QUEUE_LEN BSP_GMAC_UDP_QUEUE_SIZE

#undef MEMP_NUM_ARP_QUEUE
#define MEMP_NUM_ARP_QUEUE (2 * BSP_GMAC_UDP_QUEUE_SIZE)
#endif

#endif /* LOONGARCH_GMAC_LWIPOPTS_H */
