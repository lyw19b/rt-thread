#ifndef __LS2K_LIBAHCI_H__
#define __LS2K_LIBAHCI_H__

#include "libata.h"

enum {
    READ_CMD  = 0,
    WRITE_CMD = 1
};

// see linux/drivers/ata/ahci.h
enum {
    // scatter gather table 0x80
    // scatter gather dma 16 * 56
    AHCI_MAX_PORTS             = 32,
    AHCI_MAX_SG                = 56, // 168 /* hardware max is 64K */
    AHCI_DMA_BOUNDARY          = 0xffffffff,
    AHCI_MAX_CMDS              = 32,
    AHCI_CMD_SZ                = 32,
    AHCI_CMD_SLOT_SZ           = AHCI_MAX_CMDS * AHCI_CMD_SZ, // 32 * 32
    AHCI_RX_FIS_SZ             = 256,
    AHCI_CMD_TBL_CDB           = 0x40,
    AHCI_CMD_TBL_HDR_SZ        = 0x80,
    // 0x80 + 56 * 16
    AHCI_CMD_TBL_SZ            = AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16),
    // (0x80 + 56 * 16) * 32
    AHCI_CMD_TBL_AR_SZ         = AHCI_CMD_TBL_SZ * AHCI_MAX_CMDS,
    // 32 * 32 + (0x80 + 56 * 16) * 32 + 256
    // AHCI_PORT_PRIV_DMA_SZ      = AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ + AHCI_RX_FIS_SZ,
    AHCI_PORT_PRIV_DMA_SZ      = AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_SZ + AHCI_RX_FIS_SZ,
    AHCI_PORT_PRIV_FBS_DMA_SZ  = AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ + (AHCI_RX_FIS_SZ * 16),

    AHCI_MAX_BYTES_PER_SG = 4 * 1024 * 1024, // 4 MiB
    AHCI_MAX_BYTES_PER_TRANS = AHCI_MAX_SG * AHCI_MAX_BYTES_PER_SG,

    /* SATA global controller registers */
    // sata_host_regs
    HOST_CAP            = 0x00, /* host capabilities */
    HOST_CTL            = 0x04, /* global host control */
    HOST_IRQ_STAT       = 0x08, /* interrupt status */
    HOST_PORTS_IMPL     = 0x0c, /* bitmap of implemented ports */
    HOST_VERSION        = 0x10, /* AHCI spec. version compliancy */
    HOST_EM_LOC         = 0x1c, /* Enclosure Management location */
    HOST_EM_CTL         = 0x20, /* Enclosure Management Control */
    HOST_CAP2           = 0x24, /* host capabilities, extended */

    /* HOST_CTL bits */
    HOST_RESET          = (0x1u << 0), /* reset controller; self-clear */
    HOST_IRQ_EN         = (0x1u << 1), /* global IRQ enable */
    HOST_MRSM           = (0x1u << 2), /* MSI Revert to Single Message */
    HOST_AHCI_EN        = (0x1u << 31), /* AHCI enabled */

    /* HOST_CAP bits */
    HOST_CAP_SXS        = (0x1u << 5), /* Supports External SATA */
    HOST_CAP_EMS        = (0x1u << 6), /* Enclosure Management support */
    HOST_CAP_CCC        = (0x1u << 7), /* Command Completion Coalescing */
    HOST_CAP_PART       = (0x1u << 13), /* Partial state capable */
    HOST_CAP_SSC        = (0x1u << 14), /* Slumber state capable */
    HOST_CAP_PIO_MULTI  = (0x1u << 15), /* PIO multiple DRQ support */
    HOST_CAP_FBS        = (0x1u << 16), /* FIS-based switching support */
    HOST_CAP_PMP        = (0x1u << 17), /* Port Multiplier support */
    HOST_CAP_ONLY       = (0x1u << 18), /* Supports AHCI mode only */
    HOST_CAP_CLO        = (0x1u << 24), /* Command List Override support */
    HOST_CAP_LED        = (0x1u << 25), /* Supports activity LED */
    HOST_CAP_ALPM       = (0x1u << 26), /* Aggressive Link PM support */
    HOST_CAP_SSS        = (0x1u << 27), /* Staggered Spin-up */
    HOST_CAP_MPS        = (0x1u << 28), /* Mechanical presence switch */
    HOST_CAP_SNTF       = (0x1u << 29), /* SNotification register */
    HOST_CAP_NCQ        = (0x1u << 30), /* Native Command Queueing */
    HOST_CAP_64         = (0x1u << 31), /* PCI DAC (64-bit DMA) support */

    /* HOST_CAP2 bits */
    HOST_CAP2_BOH       = (0x1u << 0), /* BIOS/OS handoff supported */
    HOST_CAP2_NVMHCI    = (0x1u << 1), /* NVMHCI supported */
    HOST_CAP2_APST      = (0x1u << 2), /* Automatic partial to slumber */
    HOST_CAP2_SDS       = (0x1u << 3), /* Support device sleep */
    HOST_CAP2_SADM      = (0x1u << 4), /* Support aggressive DevSlp */
    HOST_CAP2_DESO      = (0x1u << 5), /* DevSlp from slumber only */

    /* registers for each SATA port */
    // sata_port_regs
    PORT_LST_ADDR       = 0x00, /* command list DMA addr */
    PORT_LST_ADDR_HI    = 0x04, /* command list DMA addr hi */
    PORT_FIS_ADDR       = 0x08, /* FIS rx buf addr */
    PORT_FIS_ADDR_HI    = 0x0c, /* FIS rx buf addr hi */
    PORT_IRQ_STAT       = 0x10, /* interrupt status */
    PORT_IRQ_MASK       = 0x14, /* interrupt enable/disable mask */
    PORT_CMD            = 0x18, /* port command */
    PORT_TFDATA         = 0x20, /* taskfile data */
    PORT_SIG            = 0x24, /* device TF signature */
    PORT_CMD_ISSUE      = 0x38, /* command issue */
    PORT_SCR_STAT       = 0x28, /* SATA phy register: SStatus */
    PORT_SCR_CTL        = 0x2c, /* SATA phy register: SControl */
    PORT_SCR_ERR        = 0x30, /* SATA phy register: SError */
    PORT_SCR_ACT        = 0x34, /* SATA phy register: SActive */
    PORT_SCR_NTF        = 0x3c, /* SATA phy register: SNotification */
    PORT_FBS            = 0x40, /* FIS-based Switching */
    PORT_DEVSLP         = 0x44, /* device sleep */

    /* PORT_IRQ_{STAT,MASK} bits */
    PORT_IRQ_COLD_PRES     = (0x1u << 31), /* cold presence detect */
    PORT_IRQ_TF_ERR        = (0x1u << 30), /* task file error */
    PORT_IRQ_HBUS_ERR      = (0x1u << 29), /* host bus fatal error */
    PORT_IRQ_HBUS_DATA_ERR = (0x1u << 28), /* host bus data error */
    PORT_IRQ_IF_ERR        = (0x1u << 27), /* interface fatal error */
    PORT_IRQ_IF_NONFATAL   = (0x1u << 26), /* interface non-fatal error */
    PORT_IRQ_OVERFLOW      = (0x1u << 24), /* xfer exhausted available S/G */
    PORT_IRQ_BAD_PMP       = (0x1u << 23), /* incorrect port multiplier */

    PORT_IRQ_PHYRDY      = (0x1u << 22), /* PhyRdy changed */
    PORT_IRQ_DEV_ILCK    = (0x1u << 7), /* device interlock */
    PORT_IRQ_CONNECT     = (0x1u << 6), /* port connect change status */
    PORT_IRQ_SG_DONE     = (0x1u << 5), /* descriptor processed */
    PORT_IRQ_UNK_FIS     = (0x1u << 4), /* unknown FIS rx'd */
    PORT_IRQ_SDB_FIS     = (0x1u << 3), /* Set Device Bits FIS rx'd */
    PORT_IRQ_DMAS_FIS    = (0x1u << 2), /* DMA Setup FIS rx'd */
    PORT_IRQ_PIOS_FIS    = (0x1u << 1), /* PIO Setup FIS rx'd */
    PORT_IRQ_D2H_REG_FIS = (0x1u << 0), /* D2H Register FIS rx'd */

    PORT_IRQ_FREEZE     = PORT_IRQ_HBUS_ERR | PORT_IRQ_IF_ERR | PORT_IRQ_CONNECT |
                          PORT_IRQ_PHYRDY | PORT_IRQ_UNK_FIS | PORT_IRQ_BAD_PMP,
    PORT_IRQ_ERROR      = PORT_IRQ_FREEZE | PORT_IRQ_TF_ERR | PORT_IRQ_HBUS_DATA_ERR,
    DEF_PORT_IRQ        = PORT_IRQ_ERROR | PORT_IRQ_SG_DONE | PORT_IRQ_SDB_FIS |
                          PORT_IRQ_DMAS_FIS | PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS,

    /* PORT_CMD bits */
    PORT_CMD_ASP         = (0x1u << 27), /* Aggressive Slumber/Partial */
    PORT_CMD_ALPE        = (0x1u << 26), /* Aggressive Link PM enable */
    PORT_CMD_ATAPI       = (0x1u << 24), /* Device is ATAPI */
    PORT_CMD_FBSCP       = (0x1u << 22), /* FBS Capable Port */
    PORT_CMD_ESP         = (0x1u << 21), /* External Sata Port */
    PORT_CMD_CPD         = (0x1u << 20), /* Cold Presence Detection */
    PORT_CMD_MPSP        = (0x1u << 19), /* Mechanical Presence Switch */
    PORT_CMD_HPCP        = (0x1u << 18), /* HotPlug Capable Port */
    PORT_CMD_PMP         = (0x1u << 17), /* PMP attached */
    PORT_CMD_LIST_ON     = (0x1u << 15), /* cmd list DMA engine running */
    PORT_CMD_FIS_ON      = (0x1u << 14), /* FIS DMA engine running */
    PORT_CMD_FIS_RX      = (0x1u << 4), /* Enable FIS receive DMA engine */
    PORT_CMD_CLO         = (0x1u << 3), /* Command list override */
    PORT_CMD_POWER_ON    = (0x1u << 2), /* Power up device */
    PORT_CMD_SPIN_UP     = (0x1u << 1), /* Spin up device */
    PORT_CMD_START       = (0x1u << 0), /* Enable port DMA engine */

    PORT_CMD_ICC_MASK    = (0xfu << 28), /* i/f ICC state mask */
    PORT_CMD_ICC_ACTIVE  = (0x1u << 28), /* Put i/f in active state */
    PORT_CMD_ICC_PARTIAL = (0x2u << 28), /* Put i/f in partial state */
    PORT_CMD_ICC_SLUMBER = (0x6u << 28), /* Put i/f in slumber state */

    /* PORT_CMD capabilities mask */
    PORT_CMD_CAP         = PORT_CMD_HPCP | PORT_CMD_MPSP | PORT_CMD_CPD |
                           PORT_CMD_ESP | PORT_CMD_FBSCP,
};

enum {
    SATA_FLAG_WCACHE = 0x00000100,
    SATA_FLAG_FLUSH = 0x00000200,
    SATA_FLAG_FLUSH_EXT = 0x00000400,
};

struct ahci_cmd_hdr
{
    uint32_t opts;
    uint32_t status;
    uint32_t tbl_addr_lo;
    uint32_t tbl_addr_hi;
    uint32_t reserved[4];
};

struct ahci_sg
{
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t reserved;
    uint32_t flags_size;
};

struct ahci_ioport
{
    uint64_t port_mmio; // address of port reg

    struct ahci_cmd_hdr *cmd_slot;
    uint64_t cmd_slot_dma;

    uint64_t rx_fis;
    uint64_t rx_fis_dma;

    uint64_t cmd_tbl;
    uint64_t cmd_tbl_dma;

    struct ahci_sg *cmd_tbl_sg;
};

struct ahci_blk_dev
{
    bool lba48;
    uint64_t lba;
    uint64_t blksz;

    uint32_t queue_depth; // ncq depth

    uint8_t product[ATA_ID_PROD_LEN + 1];
    uint8_t serial[ATA_ID_SERNO_LEN + 1];
    uint8_t revision[ATA_ID_FW_REV_LEN + 1];
};

struct ahci_device
{
    uint64_t mmio_base; // address of ahci reg

    uint32_t flags;
    uint32_t cap; // HOST_CAP
    uint32_t cap2; // HOST_CAP2
    uint32_t version; // HOST_VERSION
    uint32_t port_map; // HOST_PORTS_IMPL
    uint32_t pio_mask;
    uint32_t udma_mask;

    uint8_t n_ports; // number of available ports
    uint32_t port_map_linkup; // linkup port map
    struct ahci_ioport port[AHCI_MAX_PORTS]; // 32 ports max

    // we only support one port / block device
    uint8_t port_idx; // index of the active port
    struct ahci_blk_dev blk_dev;
};

#endif // __LS2K_LIBAHCI_H__
