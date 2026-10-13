#ifndef __LS2K_ETH_PLATFORM_H__
#define __LS2K_ETH_PLATFORM_H__

#include <stdbool.h>
#include <stdint.h>

// printf
int eth_printf(const char *fmt, ...);

// memcpy
void *plat_memcpy(void *dest, const void *src, uint64_t n);

// aligned malloc
uint64_t eth_malloc_align(uint64_t size, uint32_t align);

// free memory
void plat_free(void *buffer);

// sync all dcache data
void eth_sync_dcache();

// convert virtual address to physical address
uint32_t eth_virt_to_phys(uint64_t va);

// convert physical address to virtual address
uint64_t eth_phys_to_virt(uint32_t pa);

// copy p to buffer
// return length
uint32_t eth_handle_tx_buffer(uint64_t p, uint64_t buffer);

// allocate a mem region and copy from buffer length bytes
// return the new mem region
uint64_t eth_handle_rx_buffer(uint64_t buffer, uint32_t length);

void eth_rx_ready(struct net_device *gmacdev);

void eth_update_linkstate(struct net_device *gmacdev, uint32_t status);

void eth_isr_install();

#endif // __LS2K_ETH_PLATFORM_H__
