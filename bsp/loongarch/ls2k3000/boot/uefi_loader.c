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
 *   Reserve a bounded kernel arena, retain firmware resources and exit UEFI.
 *   The embedded kernel makes the same EFI image usable from disk or firmware
 *   network boot. Firmware services are never used after ExitBootServices.
 */

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Acpi.h>
#include "boot_info.h"
#include "uefi_acpi.h"

extern const unsigned char ls2k3000_payload_start[] __attribute__((visibility("hidden")));
extern const unsigned char ls2k3000_payload_end[] __attribute__((visibility("hidden")));
extern void ls2k3000_efi_enter(void) __attribute__((noreturn));
extern void ls2k3000_efi_stop(void) __attribute__((noreturn));

#define MAP_CAPACITY (256U * 1024U)

static void clear_bytes(void *target, UINTN bytes)
{
    unsigned char *data = target;

    for (UINTN index = 0; index < bytes; ++index)
    {
        data[index] = 0;
    }
}

static void print(EFI_SYSTEM_TABLE *table, CHAR16 *text)
{
    if (table->ConOut)
    {
        table->ConOut->OutputString(table->ConOut, text);
    }
}

static void get_graphics(EFI_BOOT_SERVICES *services, struct ls2k3000_boot_info *info)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS status;

    status = services->LocateHandleBuffer(ByProtocol, &guid, NULL, &count, &handles);
    info->gop_status = status;
    info->gop_handles = count;

    if (EFI_ERROR(status))
    {
        return;
    }

    for (UINTN index = 0; index < count; ++index)
    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
        struct ls2k3000_framebuffer candidate;

        status = services->HandleProtocol(handles[index], &guid, (VOID **)&gop);

        if (EFI_ERROR(status) || !gop->Mode || !gop->Mode->Info)
        {
            continue;
        }

        candidate.base = gop->Mode->FrameBufferBase;
        candidate.bytes = gop->Mode->FrameBufferSize;
        candidate.width = gop->Mode->Info->HorizontalResolution;
        candidate.height = gop->Mode->Info->VerticalResolution;
        candidate.stride = gop->Mode->Info->PixelsPerScanLine;
        candidate.format = gop->Mode->Info->PixelFormat;

        /* Keep rejected modes observable after firmware consoles disappear. */
        info->gop_raw = candidate;
        info->gop_masks[0] = gop->Mode->Info->PixelInformation.RedMask;
        info->gop_masks[1] = gop->Mode->Info->PixelInformation.GreenMask;
        info->gop_masks[2] = gop->Mode->Info->PixelInformation.BlueMask;
        info->gop_masks[3] = gop->Mode->Info->PixelInformation.ReservedMask;

        /* PixelBitMask is linear too, but accept only exact 32-bit layouts. */
        if (candidate.format == PixelBitMask && info->gop_masks[1] == 0xff00 &&
            info->gop_masks[3] == 0xff000000)
        {
            if (info->gop_masks[0] == 0xff0000 && info->gop_masks[2] == 0xff)
            {
                candidate.format = PixelBlueGreenRedReserved8BitPerColor;
            }
            else if (info->gop_masks[0] == 0xff && info->gop_masks[2] == 0xff0000)
            {
                candidate.format = PixelRedGreenBlueReserved8BitPerColor;
            }
        }

        if (!ls2k3000_framebuffer_validate(&candidate))
        {
            info->framebuffer = candidate;
            break;
        }
    }

    services->FreePool(handles);
}

static EFI_STATUS get_pci(EFI_BOOT_SERVICES *services, struct ls2k3000_boot_info *info)
{
    EFI_GUID guid = EFI_PCI_IO_PROTOCOL_GUID;
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS result;

    result = services->LocateHandleBuffer(ByProtocol, &guid, NULL, &count, &handles);

    if (result == EFI_NOT_FOUND)
    {
        return EFI_SUCCESS;
    }

    if (EFI_ERROR(result))
    {
        return result;
    }

    if (count > LS2K3000_PCI_MAX)
    {
        services->FreePool(handles);
        return EFI_OUT_OF_RESOURCES;
    }

    for (UINTN index = 0; index < count; ++index)
    {
        EFI_PCI_IO_PROTOCOL *pci;
        struct ls2k3000_pci_device *device = &info->pci[info->pci_count];
        UINTN segment, bus, slot, function;
        UINT32 config[16];

        result = services->HandleProtocol(handles[index], &guid, (VOID **)&pci);

        if (EFI_ERROR(result))
        {
            break;
        }

        result = pci->GetLocation(pci, &segment, &bus, &slot, &function);

        if (EFI_ERROR(result))
        {
            break;
        }

        if (segment > 65535 || bus > 255 || slot > 31 || function > 7)
        {
            result = EFI_UNSUPPORTED;
            break;
        }

        result = pci->Pci.Read(pci, EfiPciIoWidthUint32, 0, 16, config);

        if (EFI_ERROR(result))
        {
            break;
        }

        device->segment = segment;
        device->bus = bus;
        device->devfn = (slot << 3) | function;
        device->id = config[0];
        device->class_revision = config[2];
        device->header_type = (config[3] >> 16) & 0x7f;

        for (UINT8 bar = 0; bar < (device->header_type == 0 ? 6 : 2); ++bar)
        {
            VOID *resources = NULL;
            EFI_STATUS status = pci->GetBarAttributes(pci, bar, NULL, &resources);

            if (!EFI_ERROR(status) && resources)
            {
                EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *resource = resources;

                if (resource->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR &&
                    resource->Len == sizeof(*resource) - 3 && resource->ResType == 0 &&
                    resource->AddrLen &&
                    resource->AddrRangeMin <= UINT64_MAX - resource->AddrTranslationOffset)
                {
                    UINT64 base = resource->AddrRangeMin + resource->AddrTranslationOffset;

                    if (base < (UINT64_C(1) << 40) &&
                        resource->AddrLen <= (UINT64_C(1) << 40) - base)
                    {
                        device->bar[bar].base = base;
                        device->bar[bar].bytes = resource->AddrLen;
                    }
                }

                services->FreePool(resources);
            }
        }

        ++info->pci_count;
    }

    services->FreePool(handles);
    return result;
}

static EFI_STATUS normalize_map(struct ls2k3000_boot_info *info,
                                const EFI_MEMORY_DESCRIPTOR *map,
                                UINTN bytes, UINTN stride, UINT32 version)
{
    if (bytes > MAP_CAPACITY || version != EFI_MEMORY_DESCRIPTOR_VERSION || stride < sizeof(*map) ||
        stride > 4096 || bytes % stride || bytes / stride > LS2K3000_MEMORY_MAX)
    {
        return EFI_UNSUPPORTED;
    }

    clear_bytes(info->memory, sizeof(info->memory));
    info->memory_count = bytes / stride;

    for (UINTN index = 0; index < info->memory_count; ++index)
    {
        const EFI_MEMORY_DESCRIPTOR *entry = (const VOID *)((const UINT8 *)map + index * stride);
        struct ls2k3000_memory *memory = &info->memory[index];

        memory->base = entry->PhysicalStart;
        memory->pages = entry->NumberOfPages;
        memory->type = entry->Type;
        memory->attributes = entry->Attribute;
    }

    info->checksum = ls2k3000_boot_checksum(info);
    return ls2k3000_boot_validate(info) ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    EFI_PHYSICAL_ADDRESS allocation = LS2K3000_KERNEL_PHYS;
    EFI_BOOT_SERVICES *services;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    struct ls2k3000_boot_info *info;
    EFI_STATUS status;
    UINTN payload_bytes = ls2k3000_payload_end - ls2k3000_payload_start;
    UINTN bytes, key, stride;
    UINT32 version;

    if (!table || table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        !table->BootServices || !payload_bytes || payload_bytes > LS2K3000_IMAGE_MAX)
    {
        return EFI_INVALID_PARAMETER;
    }

    services = table->BootServices;
    print(table, L"LS2K3000 RT-Thread EFI loader\r\n");
    status = services->AllocatePages(AllocateAddress, EfiLoaderData,
                                     LS2K3000_RESERVED_BYTES / 4096, &allocation);

    if (EFI_ERROR(status))
    {
        print(table, L"Kernel arena unavailable; firmware memory preserved.\r\n");
        return status;
    }

    if (allocation != LS2K3000_KERNEL_PHYS)
    {
        services->FreePages(allocation, LS2K3000_RESERVED_BYTES / 4096);
        return EFI_COMPROMISED_DATA;
    }

    info = (VOID *)(UINTN)LS2K3000_HANDOFF_PHYS;
    clear_bytes(info, sizeof(*info));
    info->magic = LS2K3000_BOOT_MAGIC;
    info->version = LS2K3000_BOOT_VERSION;
    info->bytes = sizeof(*info);
    info->kernel_base = allocation;
    info->reserved_bytes = LS2K3000_RESERVED_BYTES;
    info->kernel_bytes = payload_bytes;
    ls2k3000_efi_get_reset(table, info);
    get_graphics(services, info);
    status = get_pci(services, info);

    if (EFI_ERROR(status))
    {
        goto fail;
    }

    status = services->AllocatePool(EfiLoaderData, MAP_CAPACITY, (VOID **)&map);

    if (EFI_ERROR(status))
    {
        goto fail;
    }

    for (UINTN index = 0; index < payload_bytes; ++index)
    {
        ((UINT8 *)(UINTN)allocation)[index] = ls2k3000_payload_start[index];
    }

    print(table, L"Kernel copied. Exiting firmware boot services.\r\n");

    for (unsigned int attempt = 0; attempt < 3; ++attempt)
    {
        bytes = MAP_CAPACITY;
        status = services->GetMemoryMap(&bytes, map, &key, &stride, &version);

        if (!EFI_ERROR(status))
        {
            status = normalize_map(info, map, bytes, stride, version);
        }

        if (EFI_ERROR(status))
        {
            if (attempt == 0)
            {
                goto fail;
            }

            ls2k3000_efi_stop();
        }

        status = services->ExitBootServices(image, key);

        if (!EFI_ERROR(status))
        {
            ls2k3000_efi_enter();
        }

        if (status != EFI_INVALID_PARAMETER)
        {
            ls2k3000_efi_stop();
        }

        /* The only further firmware call before retrying is GetMemoryMap. */
    }

    ls2k3000_efi_stop();

fail:
    if (map)
    {
        services->FreePool(map);
    }

    services->FreePages(allocation, LS2K3000_RESERVED_BYTES / 4096);
    print(table, L"RT-Thread load failed before handoff; returned to firmware.\r\n");
    return status;
}
