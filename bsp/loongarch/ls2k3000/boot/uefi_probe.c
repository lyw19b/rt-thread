/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: uefi_probe.c
 * Author: LoongsonLab
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               inspect UEFI resources before handoff
 */

/*
 * Description:
 *   Inspect firmware-owned resources before implementing the kernel handoff.
 *   No disk writes, PCI BAR sizing, mode changes or ExitBootServices calls.
 */

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleNetwork.h>

static EFI_SYSTEM_TABLE *system_table;

static void output(const char *text)
{
    CHAR16 buffer[128];
    UINTN count;

    while (*text)
    {
        count = 0;

        while (*text && count < 126)
        {
            if (*text == '\n')
            {
                buffer[count++] = '\r';
            }

            buffer[count++] = (UINT8)*text++;
        }

        buffer[count] = 0;
        system_table->ConOut->OutputString(system_table->ConOut, buffer);
    }
}

static void number(UINT64 value)
{
    char buffer[19];
    static const char digits[] = "0123456789abcdef";

    buffer[0] = '0';
    buffer[1] = 'x';

    for (UINTN index = 0; index < 16; ++index)
    {
        buffer[2 + index] = digits[(value >> ((15 - index) * 4)) & 15];
    }

    buffer[18] = 0;
    output(buffer);
}

static void field(const char *name, UINT64 value)
{
    output(" ");
    output(name);
    output("=");
    number(value);
}

static EFI_STATUS handles(EFI_GUID *guid, UINTN *count, EFI_HANDLE **list)
{
    *count = 0;
    *list = NULL;

    return system_table->BootServices->LocateHandleBuffer(ByProtocol, guid,
                                                         NULL, count, list);
}

static void graphics(void)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_HANDLE *list;
    EFI_STATUS status;
    UINTN count;

    status = handles(&guid, &count, &list);
    output("GOP");
    field("status", status);
    field("handles", count);
    output("\n");

    if (EFI_ERROR(status))
    {
        return;
    }

    for (UINTN index = 0; index < count; ++index)
    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

        status = system_table->BootServices->HandleProtocol(list[index], &guid,
                                                            (VOID **)&gop);

        if (EFI_ERROR(status) || !gop->Mode || !gop->Mode->Info)
        {
            continue;
        }

        output("GOP_MODE");
        field("index", index);
        field("mode", gop->Mode->Mode);
        field("width", gop->Mode->Info->HorizontalResolution);
        field("height", gop->Mode->Info->VerticalResolution);
        field("stride", gop->Mode->Info->PixelsPerScanLine);
        field("format", gop->Mode->Info->PixelFormat);
        field("red_mask", gop->Mode->Info->PixelInformation.RedMask);
        field("green_mask", gop->Mode->Info->PixelInformation.GreenMask);
        field("blue_mask", gop->Mode->Info->PixelInformation.BlueMask);
        field("base", gop->Mode->FrameBufferBase);
        field("bytes", gop->Mode->FrameBufferSize);
        output("\n");
    }

    system_table->BootServices->FreePool(list);
}

static void pci_devices(void)
{
    EFI_GUID guid = EFI_PCI_IO_PROTOCOL_GUID;
    EFI_HANDLE *list;
    EFI_STATUS status;
    UINTN count;

    status = handles(&guid, &count, &list);
    output("PCI");
    field("status", status);
    field("handles", count);
    output("\n");

    if (EFI_ERROR(status))
    {
        return;
    }

    for (UINTN index = 0; index < count; ++index)
    {
        EFI_PCI_IO_PROTOCOL *pci;
        UINTN segment, bus, device, function;
        UINT32 header[16];

        status = system_table->BootServices->HandleProtocol(list[index], &guid,
                                                            (VOID **)&pci);

        if (EFI_ERROR(status))
        {
            continue;
        }

        status = pci->GetLocation(pci, &segment, &bus, &device, &function);

        if (!EFI_ERROR(status))
        {
            status = pci->Pci.Read(pci, EfiPciIoWidthUint32, 0, 16, header);
        }

        if (EFI_ERROR(status))
        {
            output("PCI_READ_ERROR");
            field("status", status);
            output("\n");
            continue;
        }

        output("PCI_DEVICE");
        field("segment", segment);
        field("bus", bus);
        field("device", device);
        field("function", function);
        field("id", header[0]);
        field("class_rev", header[2]);
        output("\nPCI_CONFIG");

        for (UINTN word = 0; word < 16; ++word)
        {
            output(" ");
            number(header[word]);
        }

        output("\n");
    }

    system_table->BootServices->FreePool(list);
}

static void block_devices(void)
{
    EFI_GUID guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_HANDLE *list;
    EFI_STATUS status;
    UINTN count;

    status = handles(&guid, &count, &list);
    output("BLOCK");
    field("status", status);
    field("handles", count);
    output("\n");

    if (EFI_ERROR(status))
    {
        return;
    }

    for (UINTN index = 0; index < count; ++index)
    {
        EFI_BLOCK_IO_PROTOCOL *block;

        status = system_table->BootServices->HandleProtocol(list[index], &guid,
                                                            (VOID **)&block);

        if (EFI_ERROR(status) || !block->Media)
        {
            continue;
        }

        output("BLOCK_MEDIA");
        field("index", index);
        field("present", block->Media->MediaPresent);
        field("partition", block->Media->LogicalPartition);
        field("readonly", block->Media->ReadOnly);
        field("block_bytes", block->Media->BlockSize);
        field("last_lba", block->Media->LastBlock);
        output("\n");
    }

    system_table->BootServices->FreePool(list);
}

static void network(void)
{
    EFI_GUID guid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    EFI_HANDLE *list;
    EFI_STATUS status;
    UINTN count;

    status = handles(&guid, &count, &list);
    output("NETWORK");
    field("status", status);
    field("handles", count);
    output("\n");

    if (!EFI_ERROR(status))
    {
        system_table->BootServices->FreePool(list);
    }
}

static EFI_STATUS memory_map(void)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    EFI_STATUS status;
    UINTN bytes = 0, allocated, key, stride = 0;
    UINT32 version;

    status = system_table->BootServices->GetMemoryMap(&bytes, NULL, &key,
                                                     &stride, &version);

    if (status != EFI_BUFFER_TOO_SMALL || stride < sizeof(*map))
    {
        return EFI_DEVICE_ERROR;
    }

    /* Bound firmware-supplied sizes and allow for the allocation itself. */
    if (bytes > 1024 * 1024 || stride > 4096)
    {
        return EFI_OUT_OF_RESOURCES;
    }

    allocated = bytes + 16 * stride;
    status = system_table->BootServices->AllocatePool(EfiLoaderData, allocated,
                                                      (VOID **)&map);

    if (EFI_ERROR(status))
    {
        return status;
    }

    bytes = allocated;
    status = system_table->BootServices->GetMemoryMap(&bytes, map, &key,
                                                     &stride, &version);

    if (!EFI_ERROR(status) && stride >= sizeof(*map) &&
        bytes <= allocated && bytes % stride == 0)
    {
        output("MEMORY_MAP");
        field("descriptor_bytes", stride);
        field("version", version);
        output("\n");

        for (UINTN offset = 0; offset < bytes; offset += stride)
        {
            EFI_MEMORY_DESCRIPTOR *entry = (VOID *)((UINT8 *)map + offset);

            output("MEMORY");
            field("type", entry->Type);
            field("physical", entry->PhysicalStart);
            field("pages", entry->NumberOfPages);
            field("attributes", entry->Attribute);
            output("\n");
        }
    }
    else if (!EFI_ERROR(status))
    {
        status = EFI_COMPROMISED_DATA;
    }

    system_table->BootServices->FreePool(map);
    return status;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    EFI_STATUS status;

    (void)image;

    if (!table || table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        !table->BootServices || !table->ConOut)
    {
        return EFI_INVALID_PARAMETER;
    }

    system_table = table;
    output("LS2K3000 UEFI RESOURCE PROBE v1\n");
    field("uefi_revision", table->Hdr.Revision);
    field("firmware_revision", table->FirmwareRevision);
    output("\n");
    graphics();
    pci_devices();
    block_devices();
    network();
    status = memory_map();
    output("PROBE_COMPLETE");
    field("memory_status", status);
    output("\nReturning to firmware; RT-Thread has not been started.\n");

    return status;
}
