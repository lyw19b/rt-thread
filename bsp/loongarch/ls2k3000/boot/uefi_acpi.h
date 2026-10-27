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
 *   Retain a validated Explorer reset register before leaving firmware.
 */

#ifndef LS2K3000_UEFI_ACPI_H
#define LS2K3000_UEFI_ACPI_H

#include <Uefi.h>
#include "boot_info.h"

void ls2k3000_efi_get_reset(EFI_SYSTEM_TABLE *table, struct ls2k3000_boot_info *info);

#endif
