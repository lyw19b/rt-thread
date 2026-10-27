/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: ls2k3000_pci.h
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Access firmware-assigned PCI resources without reallocating BARs or bridges.
 */

#ifndef LS2K3000_PCI_H
#define LS2K3000_PCI_H

#include "board.h"

const struct ls2k3000_pci_device *ls2k3000_pci_find(uint32_t class_code, unsigned int instance);
int ls2k3000_pci_map(const struct ls2k3000_pci_device *device, unsigned int bar,
                     uint64_t minimum, rt_ubase_t *base);
int ls2k3000_pci_command(const struct ls2k3000_pci_device *device,
                         uint16_t set, uint16_t clear);

#endif
