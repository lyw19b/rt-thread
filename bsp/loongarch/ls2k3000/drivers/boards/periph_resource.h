/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Resolve board peripheral resources without changing firmware BARs.
 * Notes:
 *   Missing or invalid firmware resources are reported to the caller.
 */

#ifndef LS2K3000_PERIPH_RESOURCE_H
#define LS2K3000_PERIPH_RESOURCE_H

#include <rtthread.h>
#include "periph_io.h"

int ls2k3000_periph_map(uint16_t pci_id, unsigned int offset,
                       unsigned int bytes, struct ls_periph_io *io);
void ls2k3000_periph_fixed(uint64_t address, struct ls_periph_io *io);
rt_err_t ls2k3000_periph_error(int result);

#endif
