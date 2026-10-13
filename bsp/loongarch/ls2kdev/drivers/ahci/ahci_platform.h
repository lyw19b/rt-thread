#ifndef __LS2K_AHCI_PLATFORM_H__
#define __LS2K_AHCI_PLATFORM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ahci_mdelay(uint32_t ms);

int ahci_printf(const char *fmt, ...);

void *ahci_memset(void *s, int c, uint64_t count);

void *ahci_memcpy(void *dest, const void *src, uint64_t n);

uint64_t ahci_malloc_align(uint64_t size, uint32_t align);

// sync all dcache data
void ahci_sync_dcache();

uint64_t ahci_phys_to_uncached(uint64_t va);

// convert virtual address to physical address
// ahci sata can accept 64bit dma address
uint64_t ahci_virt_to_phys(uint64_t va);

#endif // __LS2K_AHCI_PLATFORM_H__
