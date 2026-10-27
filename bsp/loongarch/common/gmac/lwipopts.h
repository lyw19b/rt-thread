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

#ifdef BSP_USING_GMAC
#undef DEFAULT_UDP_RECVMBOX_SIZE
#define DEFAULT_UDP_RECVMBOX_SIZE BSP_GMAC_UDP_QUEUE_SIZE

#undef MEMP_NUM_NETBUF
#define MEMP_NUM_NETBUF (2 * BSP_GMAC_UDP_QUEUE_SIZE)

#undef RECV_BUFSIZE_DEFAULT
#define RECV_BUFSIZE_DEFAULT (BSP_GMAC_UDP_QUEUE_SIZE * 1472)
#endif

#endif /* LOONGARCH_GMAC_LWIPOPTS_H */
