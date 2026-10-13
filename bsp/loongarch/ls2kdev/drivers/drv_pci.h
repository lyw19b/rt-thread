#ifndef __LS2K_PCI_H__
#define __LS2K_PCI_H__

#include <rttypes.h>

struct pci_header
{
    uint16_t VendorID;
    uint16_t DeviceID;
    uint16_t Command;
    uint16_t Status;
    uint32_t RevisionID : 8;
    uint32_t ClassCode : 24;
    uint8_t CachelineSize;
    uint8_t LatencyTimer;
    uint8_t HeaderType;
    uint8_t BIST;
    uint32_t BaseAddressRegister[6];
    uint32_t CardbusCISPointer;
    uint16_t SubsystemVendorID;
    uint16_t SubsystemID;
    uint32_t ExpansionROMBaseAddress;
    uint32_t CapabilitiesPointer : 8;
    uint32_t resv1 : 24;
    uint32_t resv2;
    uint8_t InterruptLine;
    uint8_t InterruptPin;
    uint8_t Min_Gnt;
    uint8_t Max_Lat;
};

rt_uint64_t pci_get_device_map_addr(rt_uint64_t bus, rt_uint64_t device, rt_uint64_t function, rt_uint64_t index);

#endif // __LS2K_PCI_H__
