#ifndef __LS2K_LIBATA_H__
#define __LS2K_LIBATA_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// see linux/include/linux/ata.h

enum {
    ATA_SECT_SIZE           = 512,
    ATA_MAX_SECTORS_128     = 128,
    ATA_MAX_SECTORS         = 256,
    ATA_MAX_SECTORS_1024    = 1024,
    ATA_MAX_SECTORS_LBA48   = 65535,
    ATA_MAX_SECTORS_TAPE    = 65535,

    ATA_ID_WORDS            = 256,
    ATA_ID_CONFIG           = 0,
    ATA_ID_CYLS             = 1,
    ATA_ID_HEADS            = 3,
    ATA_ID_SECTORS          = 6,
    ATA_ID_SERNO            = 10,
    ATA_ID_BUF_SIZE         = 21,
    ATA_ID_FW_REV           = 23,
    ATA_ID_PROD             = 27,
    ATA_ID_MAX_MULTSECT     = 47,
    ATA_ID_DWORD_IO         = 48, /* before ATA-8 */
    ATA_ID_TRUSTED          = 48, /* ATA-8 and later */
    ATA_ID_CAPABILITY       = 49,
    ATA_ID_OLD_PIO_MODES    = 51,
    ATA_ID_OLD_DMA_MODES    = 52,
    ATA_ID_FIELD_VALID      = 53,
    ATA_ID_CUR_CYLS         = 54,
    ATA_ID_CUR_HEADS        = 55,
    ATA_ID_CUR_SECTORS      = 56,
    ATA_ID_MULTSECT         = 59,
    ATA_ID_LBA_CAPACITY     = 60,
    ATA_ID_SWDMA_MODES      = 62,
    ATA_ID_MWDMA_MODES      = 63,
    ATA_ID_PIO_MODES        = 64,
    ATA_ID_EIDE_DMA_MIN     = 65,
    ATA_ID_EIDE_DMA_TIME    = 66,
    ATA_ID_EIDE_PIO         = 67,
    ATA_ID_EIDE_PIO_IORDY   = 68,
    ATA_ID_ADDITIONAL_SUPP  = 69,
    ATA_ID_QUEUE_DEPTH      = 75,
    ATA_ID_SATA_CAPABILITY  = 76,
    ATA_ID_SATA_CAPABILITY_2= 77,
    ATA_ID_FEATURE_SUPP     = 78,
    ATA_ID_MAJOR_VER        = 80,
    ATA_ID_COMMAND_SET_1    = 82,
    ATA_ID_COMMAND_SET_2    = 83,
    ATA_ID_CFSSE            = 84,
    ATA_ID_CFS_ENABLE_1     = 85,
    ATA_ID_CFS_ENABLE_2     = 86,
    ATA_ID_CSF_DEFAULT      = 87,
    ATA_ID_UDMA_MODES       = 88,
    ATA_ID_HW_CONFIG        = 93,
    ATA_ID_SPG              = 98,
    ATA_ID_LBA_CAPACITY_2   = 100,
    ATA_ID_SECTOR_SIZE      = 106,
    ATA_ID_WWN              = 108,
    ATA_ID_LOGICAL_SECTOR_SIZE = 117, /* and 118 */
    ATA_ID_COMMAND_SET_3    = 119,
    ATA_ID_COMMAND_SET_4    = 120,
    ATA_ID_LAST_LUN         = 126,
    ATA_ID_DLF              = 128,
    ATA_ID_CSFO             = 129,
    ATA_ID_CFA_POWER        = 160,
    ATA_ID_CFA_KEY_MGMT     = 162,
    ATA_ID_CFA_MODES        = 163,
    ATA_ID_DATA_SET_MGMT    = 169,
    ATA_ID_SCT_CMD_XPORT    = 206,
    ATA_ID_ROT_SPEED        = 217,
    ATA_ID_PIO4             = (1 << 1),

    ATA_ID_SERNO_LEN        = 20,
    ATA_ID_FW_REV_LEN       = 8,
    ATA_ID_PROD_LEN         = 40,
    ATA_ID_WWN_LEN          = 8,

    /* bits in ATA command block registers */
    ATA_HOB         = (1 << 7), /* LBA48 selector */
    ATA_NIEN        = (1 << 1), /* disable-irq flag */
    ATA_LBA         = (1 << 6), /* LBA28 selector */
    ATA_DEV1        = (1 << 4), /* Select Device 1 (slave) */
    ATA_DEVICE_OBS  = (1 << 7) | (1 << 5), /* obs bits in dev reg */
    ATA_DEVCTL_OBS  = (1 << 3), /* obsolete bit in devctl reg */
    ATA_BUSY        = (1 << 7), /* BSY status bit */
    ATA_DRDY        = (1 << 6), /* device ready */
    ATA_DF          = (1 << 5), /* device fault */
    ATA_DSC         = (1 << 4), /* drive seek complete */
    ATA_DRQ         = (1 << 3), /* data request i/o */
    ATA_CORR        = (1 << 2), /* corrected data error */
    ATA_SENSE       = (1 << 1), /* sense code available */
    ATA_ERR         = (1 << 0), /* have an error */
    ATA_SRST        = (1 << 2), /* software reset */
    ATA_ICRC        = (1 << 7), /* interface CRC error */
    ATA_BBK         = ATA_ICRC, /* pre-EIDE: block marked bad */
    ATA_UNC         = (1 << 6), /* uncorrectable media error */
    ATA_MC          = (1 << 5), /* media changed */
    ATA_IDNF        = (1 << 4), /* ID not found */
    ATA_MCR         = (1 << 3), /* media change requested */
    ATA_ABORTED     = (1 << 2), /* command aborted */
    ATA_TRK0NF      = (1 << 1), /* track 0 not found */
    ATA_AMNF        = (1 << 0), /* address mark not found */
    ATAPI_LFS       = 0xF0, /* last failed sense */
    ATAPI_EOM       = ATA_TRK0NF, /* end of media */
    ATAPI_ILI       = ATA_AMNF, /* illegal length indication */
    ATAPI_IO        = (1 << 1),
    ATAPI_COD       = (1 << 0),

    /* ATA device commands */
    ATA_CMD_DEV_RESET           = 0x08, /* ATAPI device reset */
    ATA_CMD_CHK_POWER           = 0xE5, /* check power mode */
    ATA_CMD_STANDBY             = 0xE2, /* place in standby power mode */
    ATA_CMD_IDLE                = 0xE3, /* place in idle power mode */
    ATA_CMD_EDD                 = 0x90, /* execute device diagnostic */
    ATA_CMD_DOWNLOAD_MICRO      = 0x92,
    ATA_CMD_DOWNLOAD_MICRO_DMA  = 0x93,
    ATA_CMD_NOP                 = 0x00,
    ATA_CMD_FLUSH               = 0xE7,
    ATA_CMD_FLUSH_EXT           = 0xEA,
    ATA_CMD_ID_ATA              = 0xEC, /* identify device */
    ATA_CMD_ID_ATAPI            = 0xA1, /* ATAPI identify device */
    ATA_CMD_SERVICE             = 0xA2,
    ATA_CMD_READ                = 0xC8, /* read DMA with retry */
    ATA_CMD_READ_EXT            = 0x25,
    ATA_CMD_READ_QUEUED         = 0x26,
    ATA_CMD_READ_STREAM_EXT     = 0x2B,
    ATA_CMD_READ_STREAM_DMA_EXT = 0x2A,
    ATA_CMD_WRITE               = 0xCA, /* write DMA with retry */
    ATA_CMD_WRITE_EXT           = 0x35,
    ATA_CMD_WRITE_QUEUED        = 0x36,
    ATA_CMD_WRITE_STREAM_EXT    = 0x3B,
    ATA_CMD_WRITE_STREAM_DMA_EXT= 0x3A,
    ATA_CMD_WRITE_FUA_EXT       = 0x3D,
    ATA_CMD_WRITE_QUEUED_FUA_EXT= 0x3E,
    ATA_CMD_FPDMA_READ          = 0x60,
    ATA_CMD_FPDMA_WRITE         = 0x61,
    ATA_CMD_NCQ_NON_DATA        = 0x63,
    ATA_CMD_FPDMA_SEND          = 0x64,
    ATA_CMD_FPDMA_RECV          = 0x65,
    ATA_CMD_PIO_READ            = 0x20, /* Read sectors with retry */
    ATA_CMD_PIO_READ_EXT        = 0x24,
    ATA_CMD_PIO_WRITE           = 0x30, /* write sectors with retry */
    ATA_CMD_PIO_WRITE_EXT       = 0x34,
    ATA_CMD_READ_MULTI          = 0xC4, /* read multiple */
    ATA_CMD_READ_MULTI_EXT      = 0x29,
    ATA_CMD_WRITE_MULTI         = 0xC5, /* write multiple */
    ATA_CMD_WRITE_MULTI_EXT     = 0x39,
    ATA_CMD_WRITE_MULTI_FUA_EXT = 0xCE,
    ATA_CMD_SET_FEATURES        = 0xEF, /* set features */
    ATA_CMD_SET_MULTI           = 0xC6, /* set multiple mode */
    ATA_CMD_PACKET              = 0xA0, /* ATAPI packet */
    ATA_CMD_VERIFY              = 0x40, /* read verify sectors with retry */
    ATA_CMD_VERIFY_EXT          = 0x42,
    ATA_CMD_WRITE_UNCORR_EXT    = 0x45,
    ATA_CMD_STANDBYNOW1         = 0xE0, /* standby immediate */
    ATA_CMD_IDLEIMMEDIATE       = 0xE1, /* idle immediate */
    ATA_CMD_SLEEP               = 0xE6, /* sleep */
    ATA_CMD_INIT_DEV_PARAMS     = 0x91, /* initialize device parameters */
    ATA_CMD_READ_NATIVE_MAX     = 0xF8,
    ATA_CMD_READ_NATIVE_MAX_EXT = 0x27,
    ATA_CMD_SET_MAX             = 0xF9,
    ATA_CMD_SET_MAX_EXT         = 0x37,
    ATA_CMD_READ_LOG_EXT        = 0x2F,
    ATA_CMD_WRITE_LOG_EXT       = 0x3F,
    ATA_CMD_READ_LOG_DMA_EXT    = 0x47,
    ATA_CMD_WRITE_LOG_DMA_EXT   = 0x57,
    ATA_CMD_TRUSTED_NONDATA     = 0x5B,
    ATA_CMD_TRUSTED_RCV         = 0x5C,
    ATA_CMD_TRUSTED_RCV_DMA     = 0x5D,
    ATA_CMD_TRUSTED_SND         = 0x5E,
    ATA_CMD_TRUSTED_SND_DMA     = 0x5F,
    ATA_CMD_PMP_READ            = 0xE4, /* read buffer */
    ATA_CMD_PMP_READ_DMA        = 0xE9,
    ATA_CMD_PMP_WRITE           = 0xE8, /* write buffer */
    ATA_CMD_PMP_WRITE_DMA       = 0xEB,
    ATA_CMD_CONF_OVERLAY        = 0xB1,
    ATA_CMD_SEC_SET_PASS        = 0xF1,
    ATA_CMD_SEC_UNLOCK          = 0xF2,
    ATA_CMD_SEC_ERASE_PREP      = 0xF3,
    ATA_CMD_SEC_ERASE_UNIT      = 0xF4,
    ATA_CMD_SEC_FREEZE_LOCK     = 0xF5, /* security freeze */
    ATA_CMD_SEC_DISABLE_PASS    = 0xF6,
    ATA_CMD_CONFIG_STREAM       = 0x51,
    ATA_CMD_SMART               = 0xB0,
    ATA_CMD_MEDIA_LOCK          = 0xDE,
    ATA_CMD_MEDIA_UNLOCK        = 0xDF,
    ATA_CMD_DSM                 = 0x06,
    ATA_CMD_CHK_MED_CRD_TYP     = 0xD1,
    ATA_CMD_CFA_REQ_EXT_ERR     = 0x03,
    ATA_CMD_CFA_WRITE_NE        = 0x38,
    ATA_CMD_CFA_TRANS_SECT      = 0x87,
    ATA_CMD_CFA_ERASE           = 0xC0,
    ATA_CMD_CFA_WRITE_MULT_NE   = 0xCD,
    ATA_CMD_REQ_SENSE_DATA      = 0x0B,
    ATA_CMD_SANITIZE_DEVICE     = 0xB4,
    ATA_CMD_ZAC_MGMT_IN         = 0x4A,
    ATA_CMD_ZAC_MGMT_OUT        = 0x9F,

    /* SETFEATURES stuff */
    SETFEATURES_XFER    = 0x03,
    XFER_UDMA_7         = 0x47,
    XFER_UDMA_6         = 0x46,
    XFER_UDMA_5         = 0x45,
    XFER_UDMA_4         = 0x44,
    XFER_UDMA_3         = 0x43,
    XFER_UDMA_2         = 0x42,
    XFER_UDMA_1         = 0x41,
    XFER_UDMA_0         = 0x40,
    XFER_MW_DMA_4       = 0x24, /* CFA only */
    XFER_MW_DMA_3       = 0x23, /* CFA only */
    XFER_MW_DMA_2       = 0x22,
    XFER_MW_DMA_1       = 0x21,
    XFER_MW_DMA_0       = 0x20,
    XFER_SW_DMA_2       = 0x12,
    XFER_SW_DMA_1       = 0x11,
    XFER_SW_DMA_0       = 0x10,
    XFER_PIO_6          = 0x0E, /* CFA only */
    XFER_PIO_5          = 0x0D, /* CFA only */
    XFER_PIO_4          = 0x0C,
    XFER_PIO_3          = 0x0B,
    XFER_PIO_2          = 0x0A,
    XFER_PIO_1          = 0x09,
    XFER_PIO_0          = 0x08,
    XFER_PIO_SLOW       = 0x00,
};

// Register - Host to Device FIS
struct sata_fis_h2d
{
    uint8_t fis_type; // 0
    uint8_t pm_port_c; // 1
    uint8_t command; // 2
    uint8_t features; // 3
    uint8_t lba_low; // 4
    uint8_t lba_mid; // 5
    uint8_t lba_high; // 6
    uint8_t device; // 7
    uint8_t lba_low_exp; // 8
    uint8_t lba_mid_exp; // 9
    uint8_t lba_high_exp; // 10
    uint8_t features_exp; // 11
    uint8_t sector_count; // 12
    uint8_t sector_count_exp; // 13
    uint8_t res1; // 13
    uint8_t control; // 14
    uint8_t res2[4];
};

// fis_type - SATA FIS type
enum sata_fis_type
{
    SATA_FIS_TYPE_REGISTER_H2D = 0x27,
    SATA_FIS_TYPE_REGISTER_D2H = 0x34,
    SATA_FIS_TYPE_DMA_ACT_D2H = 0x39,
    SATA_FIS_TYPE_DMA_SETUP_BI = 0x41,
    SATA_FIS_TYPE_DATA_BI = 0x46,
    SATA_FIS_TYPE_BIST_ACT_BI = 0x58,
    SATA_FIS_TYPE_PIO_SETUP_D2H = 0x5F,
    SATA_FIS_TYPE_SET_DEVICE_BITS_D2H = 0xA1,
};

static bool ata_id_is_ata(const uint16_t *id)
{
    return (id[ATA_ID_CONFIG] & (1 << 15)) == 0;
}
static bool ata_id_has_lba(const uint16_t *id)
{
    return (id[ATA_ID_CAPABILITY] & (1 << 9)) != 0;
}
static bool ata_id_has_dma(const uint16_t *id)
{
    return (id[ATA_ID_CAPABILITY] & (1 << 8)) != 0;
}
static bool ata_id_has_ncq(const uint16_t *id)
{
    return (id[ATA_ID_SATA_CAPABILITY] & (1 << 8)) != 0;
}
static uint32_t ata_id_queue_depth(const uint16_t *id)
{
    return (id[ATA_ID_QUEUE_DEPTH] & 0x1F) + 1;
}
static bool ata_id_removable(const uint16_t *id)
{
    return (id[ATA_ID_CONFIG] & (1 << 7)) != 0;
}
static uint32_t ata_id_u32(const uint16_t *id, uint32_t n)
{
    return ((uint32_t)(id[n + 1]) << 16) | id[n];
}
static uint64_t ata_id_u64(const uint16_t *id, uint32_t n)
{
    uint64_t val;
    val  = (uint64_t)id[n + 3] << 48;
    val |= (uint64_t)id[n + 2] << 32;
    val |= (uint64_t)id[n + 1] << 16;
    val |= (uint64_t)id[n + 0];
    return val;
}
static bool ata_id_has_flush(const uint16_t *id)
{
    if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
        return 0;
    return id[ATA_ID_COMMAND_SET_2] & (1 << 12);
}

static bool ata_id_has_flush_ext(const uint16_t *id)
{
    if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
        return 0;
    return id[ATA_ID_COMMAND_SET_2] & (1 << 13);
}

static bool ata_id_has_lba48(const uint16_t *id)
{
    if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
        return 0;
    if (!ata_id_u64(id, ATA_ID_LBA_CAPACITY_2))
        return 0;
    return id[ATA_ID_COMMAND_SET_2] & (1 << 10);
}

static bool ata_id_hpa_enabled(const uint16_t *id)
{
    // word 83 valid bits cover word 82 data
    if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
        return 0;
    // and 87 covers 85-87
    if ((id[ATA_ID_CSF_DEFAULT] & 0xC000) != 0x4000)
        return 0;
    // check command sets enabled as well as supported
    if ((id[ATA_ID_CFS_ENABLE_1] & ( 1 << 10)) == 0)
        return 0;
    return id[ATA_ID_COMMAND_SET_1] & (1 << 10);
}

static bool ata_id_has_wcache(const uint16_t *id)
{
    // word 83 valid bits cover word 82 data
    if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
        return 0;
    return id[ATA_ID_COMMAND_SET_1] & (1 << 5);
}

static bool ata_id_wcache_enabled(const uint16_t *id)
{
    if ((id[ATA_ID_CSF_DEFAULT] & 0xC000) != 0x4000)
        return 0;
    return id[ATA_ID_CFS_ENABLE_1] & (1 << 5);
}

#endif // __LS2K_LIBATA_H__
