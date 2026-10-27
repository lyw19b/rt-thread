/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: uefi_acpi.h
 * Author: LoongsonLab
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
