#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct DmaDesc {
  uint32_t status;
  uint32_t length;
  uint32_t buffer1;
  uint32_t buffer2;
};

struct net_device {
  void *parent;
  uint64_t iobase;
  uint8_t MacAddr[6];
  uint64_t MacBase;
  uint64_t DmaBase;
  uint64_t PhyBase;
  uint32_t Version;
  uint32_t TxBusy;
  uint32_t TxNext;
  uint32_t RxBusy;
  struct DmaDesc *TxDesc[128];
  struct DmaDesc *RxDesc[128];
  uint8_t *TxBuffer[128];
  uint8_t *RxBuffer[128];
  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t rx_errors;
  uint64_t tx_errors;
  uint32_t advertising;
  uint32_t LinkStatus;
  uint32_t DuplexMode;
  uint32_t Speed;
};

extern int32_t eth_init(struct net_device *gmacdev);

extern void eth_irq(struct net_device *gmacdev);

extern uint64_t eth_rx(struct net_device *gmacdev);

extern int32_t eth_tx(struct net_device *gmacdev, uint64_t pbuf);

extern uint64_t eth_handle_rx_buffer(uint64_t buffer, uint32_t length);

extern uint32_t eth_handle_tx_buffer(uint64_t p, uint64_t buffer);

extern void eth_isr_install(void);

extern uint64_t eth_malloc_align(uint64_t size, uint32_t align);

extern uint64_t eth_phys_to_virt(uint32_t pa);

extern int eth_printf(const char *fmt, ...);

extern void eth_rx_ready(struct net_device *gmacdev);

extern uint32_t eth_virt_to_phys(uint64_t va);

extern void eth_update_linkstate(struct net_device *gmacdev, uint32_t status);

extern void eth_sync_dcache(void);
