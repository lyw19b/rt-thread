/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: uefi_acpi.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Read ACPI reset information without invoking firmware runtime services.
 */

#include "uefi_acpi.h"
#include <Guid/Acpi.h>

static UINT64 little_endian(const UINT8 *data, unsigned int bytes)
{
    UINT64 value = 0;

    for (unsigned int index = 0; index < bytes; ++index)
    {
        value |= (UINT64)data[index] << (index * 8);
    }

    return value;
}

static int checksum_ok(const UINT8 *data, UINTN bytes)
{
    UINT8 sum = 0;

    for (UINTN index = 0; index < bytes; ++index)
    {
        sum = (UINT8)(sum + data[index]);
    }

    return sum == 0;
}

static int signature(const UINT8 *data, const char *expected, unsigned int bytes)
{
    for (unsigned int index = 0; index < bytes; ++index)
    {
        if (data[index] != (UINT8)expected[index])
        {
            return 0;
        }
    }

    return 1;
}

static const UINT8 *acpi_table(UINT64 address, UINTN *length)
{
    const UINT8 *data;

    if (address < 4096 || address >= (UINT64_C(1) << 40) - 36)
    {
        return NULL;
    }

    data = (const VOID *)(UINTN)address;
    *length = little_endian(data + 4, 4);

    if (*length < 36 || *length > 65536 ||
        address > (UINT64_C(1) << 40) - *length || !checksum_ok(data, *length))
    {
        return NULL;
    }

    return data;
}

void ls2k3000_efi_get_reset(EFI_SYSTEM_TABLE *table, struct ls2k3000_boot_info *info)
{
    EFI_GUID guid = EFI_ACPI_20_TABLE_GUID;

    if (!table->ConfigurationTable || table->NumberOfTableEntries > 1024)
    {
        return;
    }

    for (UINTN index = 0; index < table->NumberOfTableEntries; ++index)
    {
        EFI_CONFIGURATION_TABLE *entry = &table->ConfigurationTable[index];
        const UINT8 *rsdp = entry->VendorTable;
        const UINT8 *xsdt;
        UINTN length;

        if (!signature((const UINT8 *)&entry->VendorGuid, (const char *)&guid,
                        sizeof(guid)) || !rsdp || !signature(rsdp, "RSD PTR ", 8) ||
            !checksum_ok(rsdp, 20) || rsdp[15] < 2)
        {
            continue;
        }

        length = little_endian(rsdp + 20, 4);

        if (length < 36 || length > 4096 || !checksum_ok(rsdp, length))
        {
            continue;
        }

        xsdt = acpi_table(little_endian(rsdp + 24, 8), &length);

        if (!xsdt || !signature(xsdt, "XSDT", 4) || (length - 36) % 8 ||
            (length - 36) / 8 > 256)
        {
            continue;
        }

        for (UINTN offset = 36; offset < length; offset += 8)
        {
            UINTN bytes;
            const UINT8 *fadt = acpi_table(little_endian(xsdt + offset, 8), &bytes);
            UINT64 address;

            if (!fadt || !signature(fadt, "FACP", 4) || bytes < 129)
            {
                continue;
            }

            address = little_endian(fadt + 120, 8);

            /* Explorer V5.0.0343 describes this register but omits RESET_REG_SUP.
             * Accept only the independently verified 32-bit RST_CNT contract.
             * Other firmware layouts require explicit validation, not guessing.
             */
            if (fadt[116] != 0 || fadt[117] != 32 || fadt[118] != 0 ||
                (fadt[119] != 0 && fadt[119] != 3) ||
                address != UINT64_C(0x10150030) || fadt[128] != 1)
            {
                continue;
            }

            info->reset_address = address;
            info->reset_bits = 32;
            info->reset_value = 1;
            return;
        }
    }
}
