#ifndef __LS2K_ETH_DEV_H__
#define __LS2K_ETH_DEV_H__

#include <stdint.h>
#include <stdbool.h>

// mac can support up to 32 phys
#define GMAC_PHY_BASE        0
#define DEFAULT_PHY_BASE     0

// macbase and dmabase address
#define MACBASE     0x0000
#define DMABASE     0x1000

#define TX_DESC_NUM     128           // Tx Descriptors needed in the Descriptor queue
#define RX_DESC_NUM     128           // Rx Descriptors needed in the Descriptor queue

// 802.3 ethernet frame structure
// the default ethernet frame is 1,518/1,522 bytes
// 9,018/9,022 bytes if jumbo frame enable is set
#define ETHERNET_HEADER          14           // 6 byte Dest addr, 6 byte Src addr, 2 byte ethertype or length
#define ETHERNET_CRC             4            // Ethernet CRC
#define VLAN_TAG                 4            // optional 802.1q VLAN Tag
#define VLAN_TAG_QINQ            4            // optional 802.1ad VLAN Tag (QinQ)
#define MIN_ETHERNET_PAYLOAD     46           // Minimum Ethernet payload size (42 when 802.1q VLAN Tag present)
#define MAX_ETHERNET_PAYLOAD     1500         // Maximum Ethernet payload size
#define JUMBO_FRAME_PAYLOAD      9000         // Jumbo frame payload size

// RX Buffer size must be multiple of 4/8/16 bytes
#define BUF_SIZE_16KiB     16368
#define BUF_SIZE_8KiB      8188
#define BUF_SIZE_4KiB      4096
#define BUF_SIZE_2KiB      2048

// #define TX_BUF_SIZE     ETHERNET_HEADER + VLAN_TAG + MAX_ETHERNET_PAYLOAD + ETHERNET_CRC
// #define RX_BUF_SIZE     ETHERNET_HEADER + VLAN_TAG + MAX_ETHERNET_PAYLOAD + ETHERNET_CRC

// for alignment
#define TX_BUF_SIZE     BUF_SIZE_2KiB
#define RX_BUF_SIZE     BUF_SIZE_2KiB



// !!! NEVER TOUCH IT !!!
// for enhanced desc (4 dwords)
typedef struct DmaDescStruct
{
    uint32_t status;
    uint32_t length;  // buffer (1/2) length
    uint32_t buffer1; // buffer 1 pointer (dmaable)
    uint32_t buffer2; // buffer 2 pointer in ring or next descriptor pointer in chain (dmaable)
} DmaDesc;

// for enhanced desc (8 dwords)
// uint32_t status;
// uint32_t length;
// uint32_t buffer1;
// uint32_t buffer2;
// uint32_t extstatus;
// uint32_t reserved;
// uint32_t timestamplow;
// uint32_t timestamphigh;

struct DmaFeature {
    uint32_t mbps_10_100 : 1;        //  0
    uint32_t mbps_1000 : 1;          //  1
    uint32_t half_duplex : 1;        //  2
    uint32_t hash_filter : 1;         //  4
    uint32_t multi_addr : 1;         //  5
    uint32_t pcs : 1;                //  6
    uint32_t sma_mdio : 1;           //  8
    uint32_t pmt_remote_wake_up : 1; //  9
    uint32_t pmt_magic_frame : 1;    // 10
    uint32_t rmon : 1;               // 11
    uint32_t time_stamp : 1;         // 12
    uint32_t atime_stamp : 1;        // 13
    uint32_t eee : 1;                // 14
    uint32_t av : 1;                 // 15
    uint32_t tx_coe : 1;             // 16
    uint32_t rx_coe : 1;             // 17/18
    uint32_t rx_coe_type1 : 1;       // 17
    uint32_t rx_coe_type2 : 1;       // 18
    uint32_t rxfifo_over_2048 : 1;    // 19
    uint32_t number_rx_channel : 2;  // 20-21
    uint32_t number_tx_channel : 2;  // 22-23
    uint32_t enh_desc : 1;           // 24
};

struct net_device
{
    void *parent;                 // point to OS defined net struct

    uint64_t iobase;              // base address of MAC
    uint8_t MacAddr[6];           // mac address

    uint64_t MacBase;             // base address of GMAC
    uint64_t DmaBase;             // base address of DMA
    uint64_t PhyBase;             // phy device addr, 0 by default
    uint32_t Version;             // MAC version
    // struct DmaFeature DmaCap;     // DMA hardware feature

    uint32_t TxBusy;              // index of the first tx desc owned by DMA
    uint32_t TxNext;              // index of the first tx desc available
    uint32_t RxBusy;              // index of the first rx desc owned by DMA

    DmaDesc *TxDesc[TX_DESC_NUM]; // tx desc ptr queue
    DmaDesc *RxDesc[RX_DESC_NUM]; // rx desc ptr queue
    void *TxBuffer[TX_DESC_NUM];  // tx buffer ptr queue
    void *RxBuffer[RX_DESC_NUM];  // rx buffer ptr queue

    // net status
    uint64_t rx_packets;          // total packets received
    uint64_t tx_packets;          // total packets transmitted
    uint64_t rx_bytes;            // total bytes received
    uint64_t tx_bytes;            // total bytes transmitted
    uint64_t rx_errors;           // bad packets received
    uint64_t tx_errors;           // packet transmit problems

    // mii interface
    uint32_t advertising;         //
    uint32_t LinkStatus;          // link status
    uint32_t DuplexMode;          // duplex mode
    uint32_t Speed;               // link speed
};



enum DescMode
{
    RINGMODE    = 0x00000001,
    CHAINMODE   = 0x00000002,
};

enum BufferMode
{
    SINGLEBUF   = 0x00000001,
    DUALBUF     = 0x00000002,
};

enum MiiRegisters
{
    PHY_CONTROL_REG           = 0x0000,        /* Control Register */
    PHY_STATUS_REG            = 0x0001,        /* Status Register */
    PHY_ID_HI_REG             = 0x0002,        /* PHY Identifier High Register */
    PHY_ID_LOW_REG            = 0x0003,        /* PHY Identifier High Register */
    PHY_AN_ADV_REG            = 0x0004,        /* Auto-Negotiation Advertisement Register */
    PHY_LNK_PART_ABl_REG      = 0x0005,        /* Link Partner Ability Register (Base Page) */
    PHY_AN_EXP_REG            = 0x0006,        /* Auto-Negotiation Expansion Register */
    PHY_AN_NXT_PAGE_TX_REG    = 0x0007,        /* Next Page Transmit Register */
    PHY_LNK_PART_NXT_PAGE_REG = 0x0008,        /* Link Partner Next Page Register */
    PHY_1000BT_CTRL_REG       = 0x0009,        /* 1000BASE-T Control Register */
    PHY_1000BT_STATUS_REG     = 0x000a,        /* 1000BASE-T Status Register */
    PHY_SPECIFIC_CTRL_REG     = 0x0010,        /* Phy specific control register */
    PHY_SPECIFIC_STATUS_REG   = 0x0011,        /* Phy specific status register */
    PHY_INTERRUPT_ENABLE_REG  = 0x0012,        /* Phy interrupt enable register */
    PHY_INTERRUPT_STATUS_REG  = 0x0013,        /* Phy interrupt status register */
    PHY_EXT_PHY_SPC_CTRL      = 0x0014,        /* Extended Phy specific control */
    PHY_RX_ERR_COUNTER        = 0x0015,        /* Receive Error Counter */
    PHY_EXT_ADDR_CBL_DIAG     = 0x0016,        /* Extended address for cable diagnostic register */
    PHY_LED_CONTROL           = 0x0018,        /* LED Control */
    PHY_MAN_LED_OVERIDE       = 0x0019,        /* Manual LED override register */
    PHY_EXT_PHY_SPC_CTRL2     = 0x001a,        /* Extended Phy specific control 2 */
    PHY_EXT_PHY_SPC_STATUS    = 0x001b,        /* Extended Phy specific status */
    PHY_CBL_DIAG_REG          = 0x001c,        /* Cable diagnostic registers */
};

enum Mii_GEN_CTRL
{
    Mii_reset                = 0x8000,
    Mii_Speed_10             = 0x0000,
    Mii_Speed_100            = 0x2000,
    Mii_Speed_1000           = 0x0040,

    Mii_Duplex               = 0x0100,

    Mii_Manual_Master_Config  = 0x0800,

    Mii_Loopback             = 0x4000,
    Mii_NoLoopback           = 0x0000,
};

enum Mii_Duplex_Mode
{
    HALFDUPLEX = 1,
    FULLDUPLEX = 2,
};

enum Mii_Link_Speed
{
    SPEED10     = 1,
    SPEED100    = 2,
    SPEED1000   = 3,
};

enum Mii_Loop_Back
{
    NOLOOPBACK  = 0,
    LOOPBACK    = 1,
};

enum GmacRegisters
{
    GmacConfig              = 0x0000,    /* Mac config Register */
    GmacFrameFilter        = 0x0004,    /* Mac frame filtering controls */
    GmacHashHigh           = 0x0008,    /* Multi-cast hash table high */
    GmacHashLow            = 0x000C,    /* Multi-cast hash table low */
    GmacGmiiAddr           = 0x0010,    /* GMII address Register(ext. Phy) */
    GmacGmiiData           = 0x0014,    /* GMII data Register(ext. Phy) */
    GmacFlowControl        = 0x0018,    /* Flow control Register */
    GmacVlan               = 0x001C,    /* VLAN tag Register (IEEE 802.1Q) */

    GmacVersion            = 0x0020,    /* GMAC Core Version Register */
    GmacWakeupAddr         = 0x0028,    /* GMAC wake-up frame filter adrress reg */
    GmacPmtCtrlStatus      = 0x002C,    /* PMT control and status register */

    GmacInterruptStatus    = 0x0038,    /* Mac Interrupt ststus register */
    GmacInterruptMask      = 0x003C,    /* Mac Interrupt Mask register */

    GmacAddr0High          = 0x0040,    /* Mac address0 high Register */
    GmacAddr0Low           = 0x0044,    /* Mac address0 low Register */
    GmacAddr1High          = 0x0048,    /* Mac address1 high Register */
    GmacAddr1Low           = 0x004C,    /* Mac address1 low Register */
    GmacAddr2High          = 0x0050,    /* Mac address2 high Register */
    GmacAddr2Low           = 0x0054,    /* Mac address2 low Register */
    GmacAddr3High          = 0x0058,    /* Mac address3 high Register */
    GmacAddr3Low           = 0x005C,    /* Mac address3 low Register */
    GmacAddr4High          = 0x0060,    /* Mac address4 high Register */
    GmacAddr4Low           = 0x0064,    /* Mac address4 low Register */
    GmacAddr5High          = 0x0068,    /* Mac address5 high Register */
    GmacAddr5Low           = 0x006C,    /* Mac address5 low Register */
    GmacAddr6High          = 0x0070,    /* Mac address6 high Register */
    GmacAddr6Low           = 0x0074,    /* Mac address6 low Register */
    GmacAddr7High          = 0x0078,    /* Mac address7 high Register */
    GmacAddr7Low           = 0x007C,    /* Mac address7 low Register */
    GmacAddr8High          = 0x0080,    /* Mac address8 high Register */
    GmacAddr8Low           = 0x0084,    /* Mac address8 low Register */
    GmacAddr9High          = 0x0088,    /* Mac address9 high Register */
    GmacAddr9Low           = 0x008C,    /* Mac address9 low Register */
    GmacAddr10High         = 0x0090,    /* Mac address10 high Register */
    GmacAddr10Low          = 0x0094,    /* Mac address10 low Register */
    GmacAddr11High         = 0x0098,    /* Mac address11 high Register */
    GmacAddr11Low          = 0x009C,    /* Mac address11 low Register */
    GmacAddr12High         = 0x00A0,    /* Mac address12 high Register */
    GmacAddr12Low          = 0x00A4,    /* Mac address12 low Register */
    GmacAddr13High         = 0x00A8,    /* Mac address13 high Register */
    GmacAddr13Low          = 0x00AC,    /* Mac address13 low Register */
    GmacAddr14High         = 0x00B0,    /* Mac address14 high Register */
    GmacAddr14Low          = 0x00B4,    /* Mac address14 low Register */
    GmacAddr15High         = 0x00B8,    /* Mac address15 high Register */
    GmacAddr15Low          = 0x00BC,    /* Mac address15 low Register */
    GmacRgsmiiStatus       = 0x00D8,    /* Mac SGMII/RGMII/SMII status */

    // Time Stamp Register Map
    GmacTSControl          = 0x0700,  /* Controls the Timestamp update logic */

    GmacTSSubSecIncr       = 0x0704,  /* 8 bit value by which sub second register is incremented */

    GmacTSHigh             = 0x0708,  /* 32 bit seconds(MS) */
    GmacTSLow              = 0x070C,  /* 32 bit nano seconds(MS) */

    GmacTSHighUpdate       = 0x0710,  /* 32 bit seconds(MS) to be written/added/subtracted */
    GmacTSLowUpdate        = 0x0714,  /* 32 bit nano seconds(MS) to be writeen/added/subtracted */

    GmacTSAddend           = 0x0718,  /* Used by Software to readjust the clock frequency linearly */

    GmacTSTargetTimeHigh   = 0x071C,  /* 32 bit seconds(MS) to be compared with system time */
    GmacTSTargetTimeLow    = 0x0720,  /* 32 bit nano seconds(MS) to be compared with system time */

    GmacTSHighWord         = 0x0724,  /* Time Stamp Higher Word Register (Version 2 only) */

    GmacTSStatus           = 0x0728,  /* Time Stamp Status Register */
};

/* GmacConfig = 0x0000, Mac config Register Layout */
enum GmacConfigReg
{
    GmacTxConfig              = 0x01000000,
    GmacTxConfigDisable       = 0x00800000,     /* (TC) Disable Transmit Configuration */
    GmacTxConfigEnable        = 0x00000000,     /* Enable Transmit Configuratio */

    GmacWatchdog             = 0x00800000,
    GmacWatchdogDisable      = 0x00800000,     /* (WD) Disable watchdog timer on Rx */
    GmacWatchdogEnable       = 0x00000000,     /* Enable watchdog timer */

    GmacJabber               = 0x00400000,
    GmacJabberDisable        = 0x00400000,     /* (JD) Disable jabber timer on Tx */
    GmacJabberEnable         = 0x00000000,     /* Enable jabber timer */

    GmacFrameBurst           = 0x00200000,
    GmacFrameBurstEnable     = 0x00200000,     /* (BE) Enable frame bursting during Tx */
    GmacFrameBurstDisable    = 0x00000000,     /* Disable frame bursting */

    GmacJumboFrame           = 0x00100000,
    GmacJumboFrameEnable     = 0x00100000,     /* (JE) Enable jumbo frame for Tx */
    GmacJumboFrameDisable    = 0x00000000,     /* Disable jumbo frame */

    GmacInterFrameGap7       = 0x000E0000,     /* (IFG) Config7 - 40 bit times */
    GmacInterFrameGap6       = 0x000C0000,     /* (IFG) Config6 - 48 bit times */
    GmacInterFrameGap5       = 0x000A0000,     /* (IFG) Config5 - 56 bit times */
    GmacInterFrameGap4       = 0x00080000,     /* (IFG) Config4 - 64 bit times */
    GmacInterFrameGap3       = 0x00040000,     /* (IFG) Config3 - 72 bit times */
    GmacInterFrameGap2       = 0x00020000,     /* (IFG) Config2 - 80 bit times */
    GmacInterFrameGap1       = 0x00010000,     /* (IFG) Config1 - 88 bit times */
    GmacInterFrameGap0       = 0x00000000,     /* (IFG) Config0 - 96 bit times */

    GmacDisableCrs           = 0x00010000,
    GmacMiiGmii              = 0x00008000,
    GmacSelectMii            = 0x00008000,     /* (PS) Port Select-MII mode */
    GmacSelectGmii           = 0x00000000,     /* GMII mode */

    GmacFESpeed100           = 0x00004000,     /* (FES) Fast Ethernet speed 100Mbps */
    GmacFESpeed10            = 0x00000000,     /* 10Mbps */

    GmacRxOwn                = 0x00002000,
    GmacDisableRxOwn         = 0x00002000,     /* (DO) Disable receive own packets */
    GmacEnableRxOwn          = 0x00000000,     /* Enable receive own packets */

    GmacLoopback             = 0x00001000,
    GmacLoopbackOn           = 0x00001000,     /* (LM) Loopback mode for GMII/MII */
    GmacLoopbackOff          = 0x00000000,     /* Normal mode */

    GmacDuplex               = 0x00000800,
    GmacFullDuplex           = 0x00000800,     /* (DM) Full duplex mode */
    GmacHalfDuplex           = 0x00000000,     /* Half duplex mode */

    GmacRxIpcOffload          = 0x00000400,     /* IPC checksum offload */

    GmacRetry                = 0x00000200,
    GmacRetryDisable         = 0x00000200,     /* (DR) Disable Retry */
    GmacRetryEnable          = 0x00000000,     /* Enable retransmission as per BL */

    GmacLinkUp               = 0x00000100,     /* (LUD) Link UP */
    GmacLinkDown             = 0x00000100,     /* Link Down */

    GmacPadCrcStrip          = 0x00000080,
    GmacPadCrcStripEnable    = 0x00000080,     /* (ACS) Automatic Pad/Crc strip enable */
    GmacPadCrcStripDisable   = 0x00000000,     /* Automatic Pad/Crc stripping disable */

    GmacBackoffLimit         = 0x00000060,
    GmacBackoffLimit3        = 0x00000060,     /* (BL) Back-off limit in HD mode */
    GmacBackoffLimit2        = 0x00000040,
    GmacBackoffLimit1        = 0x00000020,
    GmacBackoffLimit0        = 0x00000000,

    GmacDeferralCheck        = 0x00000010,
    GmacDeferralCheckEnable  = 0x00000010,     /* (DC) Deferral check enable in HD mode */
    GmacDeferralCheckDisable = 0x00000000,     /* Deferral check disable */

    GmacTx                   = 0x00000008,
    GmacTxEnable             = 0x00000008,     /* (TE) Transmitter enable */
    GmacTxDisable            = 0x00000000,     /* Transmitter disable */

    GmacRx                   = 0x00000004,
    GmacRxEnable             = 0x00000004,     /* (RE) Receiver enable */
    GmacRxDisable            = 0x00000000,     /* Receiver disable */
};

/* GmacFrameFilter = 0x0004, Mac frame filtering controls Register Layout */
enum GmacFrameFilterReg
{
    GmacFilter               = 0x80000000,
    GmacFilterOff            = 0x80000000,     /* (RA)Receive all incoming packets */
    GmacFilterOn             = 0x00000000,     /* Receive filtered packets only */

    GmacHashPerfectFilter    = 0x00000400,     /* Hash or Perfect Filter enable */

    GmacSrcAddrFilter        = 0x00000200,
    GmacSrcAddrFilterEnable  = 0x00000200,     /* (SAF)Source Address Filter enable */
    GmacSrcAddrFilterDisable = 0x00000000,

    GmacSrcInvaAddrFilter    = 0x00000100,
    GmacSrcInvAddrFilterEn   = 0x00000100,     /* (SAIF)Inv Src Addr Filter enable */
    GmacSrcInvAddrFilterDis  = 0x00000000,

    GmacPassControl          = 0x000000C0,
    GmacPassControl3         = 0x000000C0,     /* (PCS)Forwards ctrl frms that pass AF */
    GmacPassControl2         = 0x00000080,     /* Forwards all control frames */
    GmacPassControl1         = 0x00000040,     /* Does not pass control frames */
    GmacPassControl0         = 0x00000000,     /* Does not pass control frames */

    GmacBroadcast            = 0x00000020,
    GmacBroadcastDisable     = 0x00000020,     /* (DBF)Disable Rx of broadcast frames */
    GmacBroadcastEnable      = 0x00000000,     /* Enable broadcast frames */

    GmacMulticastFilter      = 0x00000010,
    GmacMulticastFilterOff   = 0x00000010,     /* (PM) Pass all multicast packets */
    GmacMulticastFilterOn    = 0x00000000,     /* Pass filtered multicast packets */

    GmacDestAddrFilter       = 0x00000008,
    GmacDestAddrFilterInv    = 0x00000008,     /* (DAIF)Inverse filtering for DA */
    GmacDestAddrFilterNor    = 0x00000000,     /* Normal filtering for DA */

    GmacMcastHashFilter      = 0x00000004,
    GmacMcastHashFilterOn    = 0x00000004,     /* (HMC)perfom multicast hash filtering */
    GmacMcastHashFilterOff   = 0x00000000,     /* perfect filtering only */

    GmacUcastHashFilter      = 0x00000002,
    GmacUcastHashFilterOn    = 0x00000002,     /* (HUC)Unicast Hash filtering only */
    GmacUcastHashFilterOff   = 0x00000000,     /* perfect filtering only */

    GmacPromiscuousMode      = 0x00000001,
    GmacPromiscuousModeOn    = 0x00000001,     /* Receive all frames */
    GmacPromiscuousModeOff   = 0x00000000,     /* Receive filtered packets only */
};

/* GmacGmiiAddr = 0x0010, GMII address Register(ext. Phy) Layout */
enum GmacGmiiAddrReg
{
    GmiiDevMask              = 0x0000F800,     /* (PA)GMII device address */
    GmiiDevShift             = 11,

    GmiiRegMask              = 0x000007C0,     /* (GR)GMII register in selected Phy */
    GmiiRegShift             = 6,

    GmiiCsrClkMask           = 0x0000001C,     /* CSR Clock bit Mask */
    GmiiCsrClk5              = 0x00000014,     /* (CR)CSR Clock Range     250-300 MHz */
    GmiiCsrClk4              = 0x00000010,     /*                         150-250 MHz */
    GmiiCsrClk3              = 0x0000000C,     /*                           35-60 MHz */
    GmiiCsrClk2              = 0x00000008,     /*                           20-35 MHz */
    GmiiCsrClk1              = 0x00000004,     /*                         100-150 MHz */
    GmiiCsrClk0              = 0x00000000,     /*                          60-100 MHz */

    GmiiWrite                = 0x00000002,     /* (GW)Write to register */
    GmiiRead                 = 0x00000000,     /* Read from register */

    GmiiBusy                 = 0x00000001,     /* (GB)GMII interface is busy */
};

/* GmacGmiiData = 0x0014, GMII data Register(ext. Phy) Layout */
enum GmacGmiiDataReg
{
    GmiiDataMask             = 0x0000FFFF,     /* (GD)GMII Data */
};

/* GmacFlowControl = 0x0018, Flow control Register Layout */
enum GmacFlowControlReg
{
    GmacPauseTimeMask           = 0xFFFF0000,     /* (PT) PAUSE TIME field in the control frame */
    GmacPauseTimeShift          = 16,

    GmacPauseLowThresh          = 0x00000030,
    GmacPauseLowThresh3         = 0x00000030,     /* (PLT)thresh for pause tmr 256 slot time */
    GmacPauseLowThresh2         = 0x00000020,     /*                           144 slot time */
    GmacPauseLowThresh1         = 0x00000010,     /*                            28 slot time */
    GmacPauseLowThresh0         = 0x00000000,     /*                             4 slot time */

    GmacUnicastPauseFrame       = 0x00000008,
    GmacUnicastPauseFrameOn     = 0x00000008,     /* (UP)Detect pause frame with unicast addr */
    GmacUnicastPauseFrameOff    = 0x00000000,     /* Detect only pause frame with multicast addr */

    GmacRxFlowControl           = 0x00000004,
    GmacRxFlowControlEnable     = 0x00000004,     /* (RFE)Enable Rx flow control */
    GmacRxFlowControlDisable    = 0x00000000,     /* Disable Rx flow control */

    GmacTxFlowControl           = 0x00000002,
    GmacTxFlowControlEnable     = 0x00000002,     /* (TFE)Enable Tx flow control */
    GmacTxFlowControlDisable    = 0x00000000,     /* Disable flow control */

    GmacFlowControlBackPressure = 0x00000001,
    GmacSendPauseFrame          = 0x00000001,     /* (FCB/PBA)send pause frm/Apply back pressure */
};

/* GmacInterruptStatus = 0x0038, Mac Interrupt ststus register */
enum GmacInterruptStatus
{
    GmacTSIntSts             = 0x00000200,      /* set if int generated due to TS (Read Time Stamp Status Register to know details) */
    GmacMmcRxChksumOffload    = 0x00000080,      /* set if int generated in MMC RX CHECKSUM OFFLOAD int register */
    GmacMmcTxIntSts          = 0x00000040,      /* set if int generated in MMC TX Int register */
    GmacMmcRxIntSts          = 0x00000020,      /* set if int generated in MMC RX Int register */
    GmacMmcIntSts            = 0x00000010,      /* set if any of the above bit [7:5] is set */
    GmacPmtIntSts            = 0x00000008,      /* set whenver magic pkt/wake-on-lan frame is received */
    GmacPcsAnComplete        = 0x00000004,      /* set when AN is complete in TBI/RTBI/SGMIII phy interface */
    GmacPcsLnkStsChange      = 0x00000002,      /* set if any lnk status change in TBI/RTBI/SGMII interface */
    GmacRgmiiIntSts          = 0x00000001,      /* set if any change in lnk status of RGMII interface */
};

/* GmacInterruptMask = 0x003C, Mac Interrupt Mask register */
enum GmacInterruptMask
{
    GmacTSIntMask            = 0x00000200,    /* when set disables the time stamp interrupt generation */
    GmacPmtIntMask           = 0x00000008,    /* when set disables the assertion of PMT interrupt */
    GmacPcsAnIntMask         = 0x00000004,    /* when set disables the assertion of PCS AN complete interrupt */
    GmacPcsLnkStsIntMask     = 0x00000002,    /* when set disables the assertion of PCS lnk status change interrupt */
    GmacRgmiiIntMask         = 0x00000001,    /* when set disables the assertion of RGMII int */
};

/* GmacRgsmiiStatus = 0x00D8, Mac SGMII/RGMII/SMII status */
enum GmacRgsmiiStatus
{
    MacFalseCarrier          = 0x00000020,    /* whether SMII phy detected false carrier (only for SMII) */
    MacJabberTimeout         = 0x00000010,    /* whether jabber timeout error in received frame (only for SMII) */

    MacLinkStatus            = 0x00000008,    /* whether the link is up (1'b1) or down (1'b0) */
    MacLinkStatusOff         = 0x00000003,    /* link status offset */

    MacLinkSpeed             = 0x00000006,    /* the current speed of the link */
    MacLinkSpeed_125         = 0x00000004,    /* 10 - 125 MHz */
    MacLinkSpeed_25          = 0x00000002,    /* 01 -  25 MHz */
    MacLinkSpeed_2_5         = 0x00000000,    /* 00 - 2.5 MHz */

    MacLinkMode              = 0x00000001,    /* the current mode of operation of the link */
};

enum DmaRegisters
{
    DmaBusMode        = 0x0000,    /* CSR0 - Bus Mode Register                     */
    DmaTxPollDemand   = 0x0004,    /* CSR1 - Transmit Poll Demand Register         */
    DmaRxPollDemand   = 0x0008,    /* CSR2 - Receive Poll Demand Register          */
    DmaRxBaseAddr     = 0x000C,    /* CSR3 - Receive Descriptor list base address  */
    DmaTxBaseAddr     = 0x0010,    /* CSR4 - Transmit Descriptor list base address */
    DmaStatus         = 0x0014,    /* CSR5 - Dma status Register                   */
    DmaControl        = 0x0018,    /* CSR6 - Dma Operation Mode Register           */
    DmaInterrupt      = 0x001C,    /* CSR7 - Interrupt enable                      */
    DmaMissedFr       = 0x0020,    /* CSR8 - Missed Frame & Buffer overflow Counter */
    DmaTxCurrDesc     = 0x0048,    /* CSR18 - Current host Tx Desc Register        */
    DmaRxCurrDesc     = 0x004C,    /* CSR19 - Current host Rx Desc Register        */
    DmaTxCurrAddr     = 0x0050,    /* CSR20 - Current host transmit buffer address */
    DmaRxCurrAddr     = 0x0054,    /* CSR21 - Current host receive buffer address  */
    DmaHWFeature      = 0x0058,    /* CSR22 - HW Feature Register                  */
};

/* DmaBusMode = 0x0000, CSR0 - Bus Mode */
enum DmaBusModeReg
{
    DmaMixedBurstEnable     = 0x04000000,   /* Mixed Burst */

    DmaFixedBurstEnable     = 0x00010000,   /* Fixed Burst SINGLE, INCR4, INCR8 or INCR16 */
    DmaFixedBurstDisable    = 0x00000000,   /*             SINGLE, INCR  */

    DmaTxPriorityRatio11    = 0x00000000,   /* (PR) TX:RX DMA priority ratio 1:1 */
    DmaTxPriorityRatio21    = 0x00004000,   /* (PR) TX:RX DMA priority ratio 2:1 */
    DmaTxPriorityRatio31    = 0x00008000,   /* (PR) TX:RX DMA priority ratio 3:1 */
    DmaTxPriorityRatio41    = 0x0000C000,   /* (PR) TX:RX DMA priority ratio 4:1 */

    DmaBurstLengthx8        = 0x01000000,   /* When set mutiplies the PBL by 8 */

    DmaBurstLength256       = 0x01002000,   /* (DmaBurstLengthx8 | DmaBurstLength32) = 256 */
    DmaBurstLength128       = 0x01001000,   /* (DmaBurstLengthx8 | DmaBurstLength16) = 128 */
    DmaBurstLength64        = 0x01000800,   /* (DmaBurstLengthx8 | DmaBurstLength8) = 64 */
    DmaBurstLength32        = 0x00002000,   /* (PBL) Programmable Dma burst length = 32 */
    DmaBurstLength16        = 0x00001000,   /* Dma burst length = 16 */
    DmaBurstLength8         = 0x00000800,   /* Dma burst length = 8 */
    DmaBurstLength4         = 0x00000400,   /* Dma burst length = 4 */
    DmaBurstLength2         = 0x00000200,   /* Dma burst length = 2 */
    DmaBurstLength1         = 0x00000100,   /* Dma burst length = 1 */
    DmaBurstLength0         = 0x00000000,   /* Dma burst length = 0 */

    DmaDescriptor8DWords    = 0x00000080,   /* Enh Descriptor 1 => 8 dwords / 32 bytes */
    DmaDescriptor4DWords    = 0x00000000,   /* Enh Descriptor 0 => 4 dwords / 16 bytes */

    DmaDescriptorSkip16     = 0x00000040,   /* (DSL) Descriptor skip length (no. of dwords) */
    DmaDescriptorSkip8      = 0x00000020,   /* between two unchained descriptors */
    DmaDescriptorSkip4      = 0x00000010,
    DmaDescriptorSkip2      = 0x00000008,
    DmaDescriptorSkip1      = 0x00000004,
    DmaDescriptorSkip0      = 0x00000000,

    DmaArbitRr              = 0x00000000,   /* (DA) DMA RR arbitration */
    DmaArbitPr              = 0x00000002,   /* Rx has priority over Tx */

    DmaResetOn              = 0x00000001,   /* (SWR)Software Reset DMA engine */
    DmaResetOff             = 0x00000000,
};

/* DmaStatus = 0x0014, CSR5 - Dma Status Register */
enum DmaStatusReg
{
    GmacPmtIntr             = 0x10000000,   /* (GPI) Gmac subsystem interrupt */
    GmacMmcIntr             = 0x08000000,   /* (GMI) Gmac MMC subsystem interrupt */
    GmacLineIntfIntr        = 0x04000000,   /* Line interface interrupt */

    DmaErrorBit2            = 0x02000000,   /* (EB) Error bits 0-data buffer, 1-desc. access */
    DmaErrorBit1            = 0x01000000,   /* (EB) Error bits 0-write trnsf, 1-read transfr */
    DmaErrorBit0            = 0x00800000,   /* (EB) Error bits 0-Rx DMA, 1-Tx DMA */

    DmaTxState              = 0x00700000,   /* (TS) Transmit process state */
    DmaTxStopped            = 0x00000000,   /* Stopped - Reset or Stop Tx Command issued */
    DmaTxFetching           = 0x00100000,   /* Running - fetching the Tx descriptor */
    DmaTxWaiting            = 0x00200000,   /* Running - waiting for status */
    DmaTxReading            = 0x00300000,   /* Running - reading the data from host memory */
    DmaTxSuspended          = 0x00600000,   /* Suspended - Tx Descriptor unavailabe */
    DmaTxClosing            = 0x00700000,   /* Running - closing Rx descriptor */

    DmaRxState              = 0x000E0000,   /* (RS) Receive process state */
    DmaRxStopped            = 0x00000000,   /* Stopped - Reset or Stop Rx Command issued */
    DmaRxFetching           = 0x00020000,   /* Running - fetching the Rx descriptor */
    DmaRxWaiting            = 0x00060000,   /* Running - waiting for packet */
    DmaRxSuspended          = 0x00080000,   /* Suspended - Rx Descriptor unavailable */
    DmaRxClosing            = 0x000A0000,   /* Running - closing descriptor */
    DmaRxQueuing            = 0x000E0000,   /* Running - queuing the recieve frame into host memory */

    DmaIntNormal            = 0x00010000,   /* (NIS) Normal interrupt summary */
    DmaIntAbnormal          = 0x00008000,   /* (AIS) Abnormal interrupt summary */

    DmaIntEarlyRx           = 0x00004000,   /* Early receive interrupt (Normal) */
    DmaIntBusError          = 0x00002000,   /* Fatal bus error (Abnormal) */
    DmaIntEarlyTx           = 0x00000400,   /* Early transmit interrupt (Abnormal) */
    DmaIntRxWdogTO          = 0x00000200,   /* Receive Watchdog Timeout (Abnormal) */
    DmaIntRxStopped         = 0x00000100,   /* Receive process stopped (Abnormal) */
    DmaIntRxNoBuffer        = 0x00000080,   /* Receive buffer unavailable (Abnormal) */
    DmaIntRxCompleted       = 0x00000040,   /* Completion of frame reception (Normal) */
    DmaIntTxUnderflow        = 0x00000020,   /* Transmit underflow (Abnormal) */
    DmaIntRcvOverflow        = 0x00000010,   /* Receive Buffer overflow interrupt */
    DmaIntTxJabberTO        = 0x00000008,   /* Transmit Jabber Timeout (Abnormal) */
    DmaIntTxNoBuffer        = 0x00000004,   /* Transmit buffer unavailable (Normal) */
    DmaIntTxStopped         = 0x00000002,   /* Transmit process stopped (Abnormal) */
    DmaIntTxCompleted       = 0x00000001,   /* Transmit completed (Normal) */
};

/* DmaControl = 0x0018, CSR6 - Dma Operation Mode Register */
enum DmaControlReg
{
    DmaDisableDropTcpCs     = 0x04000000,   /* (DT) Dis. drop. of tcp/ip CS error frames */

    DmaStoreAndForward      = 0x02200000,   /* (SF) Store and forward */
    DmaFlushTxFifo          = 0x00100000,   /* (FTF) Tx FIFO controller is reset to default */

    DmaTxThreshCtrl         = 0x0001C000,   /* (TTC) Controls thre Threh of MTL tx Fifo */
    DmaTxThreshCtrl16       = 0x0001C000,   /* (TTC) Controls thre Threh of MTL tx Fifo 16 */
    DmaTxThreshCtrl24       = 0x00018000,   /* (TTC) Controls thre Threh of MTL tx Fifo 24 */
    DmaTxThreshCtrl32       = 0x00014000,   /* (TTC) Controls thre Threh of MTL tx Fifo 32 */
    DmaTxThreshCtrl40       = 0x00010000,   /* (TTC) Controls thre Threh of MTL tx Fifo 40 */
    DmaTxThreshCtrl256      = 0x0000c000,   /* (TTC) Controls thre Threh of MTL tx Fifo 256 */
    DmaTxThreshCtrl192      = 0x00008000,   /* (TTC) Controls thre Threh of MTL tx Fifo 192 */
    DmaTxThreshCtrl128      = 0x00004000,   /* (TTC) Controls thre Threh of MTL tx Fifo 128 */
    DmaTxThreshCtrl64       = 0x00000000,   /* (TTC) Controls thre Threh of MTL tx Fifo 64 */

    DmaTxStart              = 0x00002000,   /* (ST) Start/Stop transmission */

    DmaRxFlowCtrlDeact      = 0x00401800,   /* (RFD) Rx flow control deact. threhold */
    DmaRxFlowCtrlDeact1K    = 0x00000000,   /* (RFD) Rx flow control deact. threhold (1kbytes) */
    DmaRxFlowCtrlDeact2K    = 0x00000800,   /* (RFD) Rx flow control deact. threhold (2kbytes) */
    DmaRxFlowCtrlDeact3K    = 0x00001000,   /* (RFD) Rx flow control deact. threhold (3kbytes) */
    DmaRxFlowCtrlDeact4K    = 0x00001800,   /* (RFD) Rx flow control deact. threhold (4kbytes) */
    DmaRxFlowCtrlDeact5K    = 0x00400000,   /* (RFD) Rx flow control deact. threhold (4kbytes) */
    DmaRxFlowCtrlDeact6K    = 0x00400800,   /* (RFD) Rx flow control deact. threhold (4kbytes) */
    DmaRxFlowCtrlDeact7K    = 0x00401000,   /* (RFD) Rx flow control deact. threhold (4kbytes) */

    DmaRxFlowCtrlAct        = 0x00800600,   /* (RFA) Rx flow control Act. threhold */
    DmaRxFlowCtrlAct1K      = 0x00000000,   /* (RFA) Rx flow control Act. threhold (1kbytes) */
    DmaRxFlowCtrlAct2K      = 0x00000200,   /* (RFA) Rx flow control Act. threhold (2kbytes) */
    DmaRxFlowCtrlAct3K      = 0x00000400,   /* (RFA) Rx flow control Act. threhold (3kbytes) */
    DmaRxFlowCtrlAct4K      = 0x00000600,   /* (RFA) Rx flow control Act. threhold (4kbytes) */
    DmaRxFlowCtrlAct5K      = 0x00800000,   /* (RFA) Rx flow control Act. threhold (5kbytes) */
    DmaRxFlowCtrlAct6K      = 0x00800200,   /* (RFA) Rx flow control Act. threhold (6kbytes) */
    DmaRxFlowCtrlAct7K      = 0x00800400,   /* (RFA) Rx flow control Act. threhold (7kbytes) */

    DmaRxThreshCtrl         = 0x00000018,   /* (RTC) Controls thre Threh of MTL rx Fifo */
    DmaRxThreshCtrl64       = 0x00000000,   /* (RTC) Controls thre Threh of MTL tx Fifo 64 */
    DmaRxThreshCtrl32       = 0x00000008,   /* (RTC) Controls thre Threh of MTL tx Fifo 32 */
    DmaRxThreshCtrl96       = 0x00000010,   /* (RTC) Controls thre Threh of MTL tx Fifo 96 */
    DmaRxThreshCtrl128      = 0x00000018,   /* (RTC) Controls thre Threh of MTL tx Fifo 128 */

    DmaEnHwFlowCtrl         = 0x00000100,   /* (EFC) Enable HW flow control */

    DmaFwdErrorFrames       = 0x00000080,   /* (FEF) Forward error frames */
    DmaFwdUnderSzFrames     = 0x00000040,   /* (FUF) Forward undersize frames */
    DmaTxSecondFrame        = 0x00000004,   /* (OSF) Operate on second frame */
    DmaRxStart              = 0x00000002,   /* (SR) Start/Stop reception */
};

/* DmaInterrupt = 0x001C, CSR7 - Interrupt Enable Register */
enum DmaInterruptReg
{
    DmaIeNormal             = DmaIntNormal,      /* 16 Normal interrupt enable */
    DmaIeAbnormal           = DmaIntAbnormal,    /* 15 Abnormal interrupt enable */

    DmaIeEarlyRx            = DmaIntEarlyRx,     /* 14 Early receive interrupt enable */
    DmaIeBusError           = DmaIntBusError,    /* 13 Fatal bus error enable */
    DmaIeEarlyTx            = DmaIntEarlyTx,     /* 10 Early transmit interrupt enable */
    DmaIeRxWdogTO           = DmaIntRxWdogTO,    /*  9 Receive Watchdog Timeout enable */
    DmaIeRxStopped          = DmaIntRxStopped,   /*  8 Receive process stopped enable  */
    DmaIeRxNoBuffer         = DmaIntRxNoBuffer,  /*  7 Receive buffer unavailable enable */
    DmaIeRxCompleted        = DmaIntRxCompleted, /*  6 Completion of frame reception enable */
    DmaIeTxUnderflow         = DmaIntTxUnderflow,  /*  5 Transmit underflow enable */
    DmaIeRxOverflow          = DmaIntRcvOverflow,  /*  4 Receive Buffer overflow interrupt */
    DmaIeTxJabberTO         = DmaIntTxJabberTO,  /*  3 Transmit Jabber Timeout enable */
    DmaIeTxNoBuffer         = DmaIntTxNoBuffer,  /*  2 Transmit buffer unavailable enable */
    DmaIeTxStopped          = DmaIntTxStopped,   /*  1 Transmit process stopped enable */
    DmaIeTxCompleted        = DmaIntTxCompleted, /*  0 Transmit completed enable */
};

/* DmaHWFeature = 0x0058, CSR22 - HW Feature Register */
enum DmaHWFeatureReg
{
    DMA_HW_FEAT_MIISEL     = 0x00000001, /*  0 10/100 Mbps Support */
    DMA_HW_FEAT_GMIISEL    = 0x00000002, /*  1 1000 Mbps Support */
    DMA_HW_FEAT_HDSEL      = 0x00000004, /*  2 Half-Duplex Support */
    DMA_HW_FEAT_EXTHASHEN  = 0x00000008, /*  3 Expanded DA Hash Filter */
    DMA_HW_FEAT_HASHSEL    = 0x00000010, /*  4 HASH Filter */
    DMA_HW_FEAT_ADDMAC     = 0x00000020, /*  5 Multiple MAC Addr Reg */
    DMA_HW_FEAT_PCSSEL     = 0x00000040, /*  6 PCS registers */
    DMA_HW_FEAT_L3L4FLTREN = 0x00000080, /*  7 Layer 3 & Layer 4 Feature */
    DMA_HW_FEAT_SMASEL     = 0x00000100, /*  8 SMA (MDIO) Interface */
    DMA_HW_FEAT_RWKSEL     = 0x00000200, /*  9 PMT Remote Wakeup */
    DMA_HW_FEAT_MGKSEL     = 0x00000400, /* 10 PMT Magic Packet */
    DMA_HW_FEAT_MMCSEL     = 0x00000800, /* 11 RMON Module */
    DMA_HW_FEAT_TSVER1SEL  = 0x00001000, /* 12 Only IEEE 1588-2002 Timestamp */
    DMA_HW_FEAT_TSVER2SEL  = 0x00002000, /* 13 IEEE 1588-2008 Advanced Timestamp */
    DMA_HW_FEAT_EEESEL     = 0x00004000, /* 14 Energy Efficient Ethernet */
    DMA_HW_FEAT_AVSEL      = 0x00008000, /* 15 AV Feature */
    DMA_HW_FEAT_TXCOESEL   = 0x00010000, /* 16 Checksum Offload in Tx */
    DMA_HW_FEAT_RXTYP1COE  = 0x00020000, /* 17 IP COE (Type 1) in Rx */
    DMA_HW_FEAT_RXTYP2COE  = 0x00040000, /* 18 IP COE (Type 2) in Rx */
    DMA_HW_FEAT_RXFIFOSIZE = 0x00080000, /* 19 Rx FIFO > 2048 Bytes */
    DMA_HW_FEAT_RXCHCNT    = 0x00300000, /* 20-21 Number of additional Rx Channels */
    DMA_HW_FEAT_TXCHCNT    = 0x00c00000, /* 22-23 Number of additional Tx Channels */
    DMA_HW_FEAT_ENHDESSEL  = 0x01000000, /* 24 Alternate Enhanced Descriptor */
    DMA_HW_FEAT_INTTSEN    = 0x02000000, /* 25 Timestamping with Internal System Time */
    DMA_HW_FEAT_FLEXIPPSEN = 0x04000000, /* 26 Flexible Pulse-Per-Second Output */
    DMA_HW_FEAT_SAVLANINS  = 0x08000000, /* 27 Source Addr or VLAN Insertion */
    DMA_HW_FEAT_ACTPHYIF   = 0x70000000, /* 28-30 Active or Selected PHY interface */
};

enum DmaDescriptorStatus
{
    DescOwnByDma          = 0x80000000,   /* (OWN)Descriptor is owned by DMA engine */

    DescDAFilterFail      = 0x40000000,   /* (AFM)Rx - DA Filter Fail for the rx frame */

    DescFrameLengthMask   = 0x3FFF0000,   /* (FL)Receive descriptor frame length */
    DescFrameLengthShift  = 16,

    DescError             = 0x00008000,   /* (ES) Error summary bit  - OR of the follo */
    DescRxTruncated       = 0x00004000,   /* (DE) Rx - no more descriptors for receive frame */
    DescSAFilterFail      = 0x00002000,   /* (SAF) Rx - SA Filter Fail for the received frame */
    DescRxLengthError     = 0x00001000,   /* (LE) Rx - frm size not matching with len field */
    DescRxDamaged         = 0x00000800,   /* (OE) Rx - frm was damaged due to buffer overflow */
    DescRxVLANTag         = 0x00000400,   /* (VLAN) Rx - received frame is a VLAN frame */
    DescRxFirst           = 0x00000200,   /* (FS) Rx - first descriptor of the frame */
    DescRxLast            = 0x00000100,   /* (LS) Rx - last descriptor of the frame */
    DescRxLongFrame       = 0x00000080,   /* (Giant Frame) Rx - frame is longer than 1518/1522 */
    DescRxCollision       = 0x00000040,   /* (LC) Rx - late collision occurred during reception */
    DescRxFrameEther      = 0x00000020,   /* (FT) Rx - Frame type - Ethernet, otherwise 802.3 */
    DescRxWatchdog        = 0x00000010,   /* (RWT) Rx - watchdog timer expired during reception */
    DescRxMiiError        = 0x00000008,   /* (RE) Rx - error reported by MII interface */
    DescRxDribbling       = 0x00000004,   /* (DE) Rx - frame contains non int multiple of 8 bits */
    DescRxCrc             = 0x00000002,   /* (CE) Rx - CRC error */
    DescRxMacMatch        = 0x00000001,   /* (RX MAC Address) Rx mac address reg(1 to 15)match */

    DescRxEXTsts          = 0x00000001,   /* Extended Status Available (RDES4) */

    DescTxIntEnable       = 0x40000000,   /* (IC) Tx - interrupt on completion */
    DescTxLast            = 0x20000000,   /* (LS) Tx - Last segment of the frame */
    DescTxFirst           = 0x10000000,   /* (FS) Tx - First segment of the frame */
    DescTxDisableCrc      = 0x08000000,   /* (DC) Tx - Add CRC disabled (first segment only) */
    DescTxDisablePadd     = 0x04000000,   /* (DP) disable padding, added by - reyaz */

    DescTxCisMask         = 0x00c00000,   /* Tx checksum offloading control mask */
    DescTxCisBypass       = 0x00000000,   /* Checksum bypass */
    DescTxCisIpv4HdrCs    = 0x00400000,   /* IPv4 header checksum */
    DescTxCisTcpOnlyCs    = 0x00800000,   /* TCP/UDP/ICMP checksum. Pseudo header checksum is assumed to be present */
    DescTxCisTcpPseudoCs  = 0x00c00000,   /* TCP/UDP/ICMP checksum fully in hardware including pseudo header */

    TxDescEndOfRing       = 0x00200000,   /* (TER) End of descriptors ring */
    TxDescChain           = 0x00100000,   /* (TCH) Second buffer address is chain address */

    DescRxChkBit0         = 0x00000001,   /* Rx - Rx Payload Checksum Error */
    DescRxChkBit7         = 0x00000080,   /* (IPC CS ERROR) Rx - Ipv4 header checksum error */
    DescRxChkBit5         = 0x00000020,   /* (FT) Rx - Frame type - Ethernet, otherwise 802.3 */

    DescRxTSavail         = 0x00000080,   /* Time stamp available */
    DescRxFrameType       = 0x00000020,   /* (FT) Rx - Frame type - Ethernet, otherwise 802.3 */

    DescTxIpv4ChkError    = 0x00010000,   /* (IHE) Tx Ip header error */
    DescTxTimeout         = 0x00004000,   /* (JT) Tx - Transmit jabber timeout */
    DescTxFrameFlushed    = 0x00002000,   /* (FF) Tx - DMA/MTL flushed the frame due to SW flush */
    DescTxPayChkError     = 0x00001000,   /* (PCE) Tx Payload checksum Error */
    DescTxLostCarrier     = 0x00000800,   /* (LC) Tx - carrier lost during tramsmission */
    DescTxNoCarrier       = 0x00000400,   /* (NC) Tx - no carrier signal from the tranceiver */
    DescTxLateCollision   = 0x00000200,   /* (LC) Tx - transmission aborted due to collision */
    DescTxExcCollisions   = 0x00000100,   /* (EC) Tx - transmission aborted after 16 collisions */
    DescTxVLANFrame       = 0x00000080,   /* (VF) Tx - VLAN-type frame */

    DescTxCollMask        = 0x00000078,   /* (CC) Tx - Collision count */
    DescTxCollShift       = 3,

    DescTxExcDeferral     = 0x00000004,   /* (ED) Tx - excessive deferral */
    DescTxUnderflow        = 0x00000002,   /* (UF) Tx - late data arrival from the memory */
    DescTxDeferred        = 0x00000001,   /* (DB) Tx - frame transmision deferred */

    // DmaDescriptorLength     length word of DMA descriptor
    RxDisIntCompl         = 0x80000000,   /* (Disable Rx int on completion) */
    RxDescEndOfRing       = 0x00008000,   /* (TER) End of descriptors ring */
    RxDescChain           = 0x00004000,   /* (TCH) Second buffer address is chain address */

    DescSize2Mask         = 0x1FFF0000,   /* (TBS2) Buffer 2 size */
    DescSize2Shift        = 16,
    DescSize1Mask         = 0x00001FFF,   /* (TBS1) Buffer 1 size */
    DescSize1Shift        = 0,

    DescRxPtpAvail        = 0x00004000,   /* PTP snapshot available */
    DescRxPtpVer          = 0x00002000,   /* When set indicates IEEE1584 Version 2 (else Ver1) */
    DescRxPtpFrameType    = 0x00001000,   /* PTP frame type Indicates PTP sent over ethernet */
    DescRxPtpMessageType  = 0x00000F00,   /* Message Type */
    DescRxPtpNo           = 0x00000000,   /* 0000 => No PTP message received */
    DescRxPtpSync         = 0x00000100,   /* 0001 => Sync (all clock types) received */
    DescRxPtpFollowUp     = 0x00000200,   /* 0010 => Follow_Up (all clock types) received */
    DescRxPtpDelayReq     = 0x00000300,   /* 0011 => Delay_Req (all clock types) received */
    DescRxPtpDelayResp    = 0x00000400,   /* 0100 => Delay_Resp (all clock types) received */
    DescRxPtpPdelayReq    = 0x00000500,   /* 0101 => Pdelay_Req (in P to P tras clk)  or Announce in Ord and Bound clk */
    DescRxPtpPdelayResp   = 0x00000600,   /* 0110 => Pdealy_Resp(in P to P trans clk) or Management in Ord and Bound clk */
    DescRxPtpPdelayRespFP = 0x00000700,   /* 0111 => Pdealy_Resp_Follow_Up (in P to P trans clk) or Signaling in Ord and Bound clk */
    DescRxPtpIPV6         = 0x00000080,   /* Received Packet is  in IPV6 Packet */
    DescRxPtpIPV4         = 0x00000040,   /* Received Packet is  in IPV4 Packet */

    DescRxChkSumBypass    = 0x00000020,   /* When set indicates checksum offload engine
                                            is bypassed */
    DescRxIpPayloadError  = 0x00000010,   /* When set indicates 16bit IP payload CS is in error */
    DescRxIpHeaderError   = 0x00000008,   /* When set indicates 16bit IPV4 header CS is in
                                            error or IP datagram version is not consistent
                                            with Ethernet type value */
    DescRxIpPayloadType   = 0x00000007,   /* Indicate the type of payload encapsulated
                                            in IPdatagram processed by COE (Rx) */
    DescRxIpPayloadUnknown= 0x00000000,   /* Unknown or didnot process IP payload */
    DescRxIpPayloadUDP    = 0x00000001,   /* UDP */
    DescRxIpPayloadTCP    = 0x00000002,   /* TCP */
    DescRxIpPayloadICMP   = 0x00000003,   /* ICMP */
};

enum RxDescCOEEncode
{
    RxLenLT600                 = 0,    /* Bit(5:7:0)=>0 IEEE 802.3 type frame Length field is Lessthan 0x0600 */
    RxIpHdrPayLoadChkBypass    = 1,    /* Bit(5:7:0)=>1 Payload & Ip header checksum bypassed (unsuppported payload) */
    RxIpHdrPayLoadRes          = 2,    /* Bit(5:7:0)=>2 Reserved */
    RxChkBypass                = 3,    /* Bit(5:7:0)=>3 Neither IPv4 nor IPV6. So checksum bypassed */
    RxNoChkError               = 4,    /* Bit(5:7:0)=>4 No IPv4/IPv6 Checksum error detected */
    RxPayLoadChkError          = 5,    /* Bit(5:7:0)=>5 Payload checksum error detected for Ipv4/Ipv6 frames */
    RxIpHdrChkError            = 6,    /* Bit(5:7:0)=>6 Ip header checksum error detected for Ipv4 frames */
    RxIpHdrPayLoadChkError     = 7,    /* Bit(5:7:0)=>7 Payload & Ip header checksum error detected for Ipv4/Ipv6 frames */
};

enum MMC_ENABLE
{
    GmacMmcCntrl               = 0x0100,    /* mmc control for operating mode of MMC */
    GmacMmcIntrRx              = 0x0104,    /* maintains interrupts generated by rx counters */
    GmacMmcIntrTx              = 0x0108,    /* maintains interrupts generated by tx counters */
    GmacMmcIntrMaskRx          = 0x010C,    /* mask for interrupts generated from rx counters */
    GmacMmcIntrMaskTx          = 0x0110,    /* mask for interrupts generated from tx counters */
};

enum MMC_TX
{
    GmacMmcTxOctetCountGb      = 0x0114,    /* Bytes Tx excl. of preamble and retried bytes */
    GmacMmcTxFrameCountGb      = 0x0118,    /* Frames Tx excl. of retried frames */
    GmacMmcTxBcFramesG         = 0x011C,    /* Broadcast Frames Tx */
    GmacMmcTxMcFramesG         = 0x0120,    /* Multicast Frames Tx */

    GmacMmcTx64OctetsGb        = 0x0124,    /* Tx with len 64 bytes excl. of pre and retried */
    GmacMmcTx65To127OctetsGb   = 0x0128,    /* Tx with len >64 bytes <=127 excl. of pre and retried */
    GmacMmcTx128To255OctetsGb  = 0x012C,    /* Tx with len >128 bytes <=255 excl. of pre and retried */
    GmacMmcTx256To511OctetsGb  = 0x0130,    /* Tx with len >256 bytes <=511 excl. of pre and retried */
    GmacMmcTx512To1023OctetsGb = 0x0134,    /* Tx with len >512 bytes <=1023 excl. of pre and retried */
    GmacMmcTx1024ToMaxOctetsGb = 0x0138,    /* Tx with len >1024 bytes <=MaxSize excl. of pre and retried */

    GmacMmcTxUcFramesGb        = 0x013C,    /* Unicast Frames Tx */
    GmacMmcTxMcFramesGb        = 0x0140,    /* Multicast Frames Tx */
    GmacMmcTxBcFramesGb        = 0x0144,    /* Broadcast Frames Tx */
    GmacMmcTxUnderFlowError    = 0x0148,    /* Frames aborted due to Underflow error */
    GmacMmcTxSingleColG        = 0x014C,    /* Successfully Tx Frames after singel collision in Half duplex mode */
    GmacMmcTxMultiColG         = 0x0150,    /* Successfully Tx Frames after more than singel collision in Half duplex mode */
    GmacMmcTxDeferred          = 0x0154,    /* Successfully Tx Frames after a deferral in Half duplex mode */
    GmacMmcTxLateCol           = 0x0158,    /* Frames aborted due to late collision error */
    GmacMmcTxExessCol          = 0x015C,    /* Frames aborted due to excessive (16) collision errors */
    GmacMmcTxCarrierError      = 0x0160,    /* Frames aborted due to carrier sense error (No carrier or Loss of carrier) */
    GmacMmcTxOctetCountG       = 0x0164,    /* Bytes Tx excl. of preamble and retried bytes */
    GmacMmcTxFrameCountG       = 0x0168,    /* Frames Tx */
    GmacMmcTxExessDef          = 0x016C,    /* Frames aborted due to excessive deferral errors (deferred for more than 2 max-sized frame times) */

    GmacMmcTxPauseFrames       = 0x0170,    /* Number of good pause frames Tx */
    GmacMmcTxVlanFramesG       = 0x0174,    /* Number of good Vlan frames Tx excl. retried frames */
};

enum MMC_RX
{
    GmacMmcRxFrameCountGb      = 0x0180,    /* Frames Rx */
    GmacMmcRxOctetCountGb      = 0x0184,    /* Bytes Rx excl. of preamble and retried bytes */
    GmacMmcRxOctetCountG       = 0x0188,    /* Bytes Rx excl. of preamble and retried bytes */
    GmacMmcRxBcFramesG         = 0x018C,    /* Broadcast Frames Rx */
    GmacMmcRxMcFramesG         = 0x0190,    /* Multicast Frames Rx */

    GmacMmcRxCrcError          = 0x0194,    /* Number of frames received with CRC error */
    GmacMmcRxAlignError        = 0x0198,    /* Number of frames received with alignment (dribble) error. Only in 10/100mode */
    GmacMmcRxRuntError         = 0x019C,    /* Number of frames received with runt (<64 bytes and CRC error) error */
    GmacMmcRxJabberError       = 0x01A0,    /* Number of frames rx with jabber (>1518/1522 or >9018/9022 and CRC) */
    GmacMmcRxUnderSizeG        = 0x01A4,    /* Number of frames received with <64 bytes without any error */
    GmacMmcRxOverSizeG         = 0x01A8,    /* Number of frames received with >1518/1522 bytes without any error */

    GmacMmcRx64OctetsGb        = 0x01AC,    /* Rx with len 64 bytes excl. of pre and retried */
    GmacMmcRx65To127OctetsGb   = 0x01B0,    /* Rx with len >64 bytes <=127 excl. of pre and retried */
    GmacMmcRx128To255OctetsGb  = 0x01B4,    /* Rx with len >128 bytes <=255 excl. of pre and retried */
    GmacMmcRx256To511OctetsGb  = 0x01B8,    /* Rx with len >256 bytes <=511 excl. of pre and retried */
    GmacMmcRx512To1023OctetsGb = 0x01BC,    /* Rx with len >512 bytes <=1023 excl. of pre and retried */
    GmacMmcRx1024ToMaxOctetsGb = 0x01C0,    /* Rx with len >1024 bytes <=MaxSize excl. of pre and retried */

    GmacMmcRxUcFramesG         = 0x01C4,    /* Unicast Frames Rx */
    GmacMmcRxLengthError       = 0x01C8,    /* Number of frames received with Length type field != frame size */
    GmacMmcRxOutOfRangeType    = 0x01CC,    /* Number of frames received with length field != valid frame size */

    GmacMmcRxPauseFrames       = 0x01D0,    /* Number of good pause frames Rx */
    GmacMmcRxFifoOverFlow      = 0x01D4,    /* Number of missed rx frames due to FIFO overflow */
    GmacMmcRxVlanFramesGb      = 0x01D8,    /* Number of good Vlan frames Rx */

    GmacMmcRxWatchdobError     = 0x01DC,    /* Number of frames rx with error due to watchdog timeout error */
};

enum MMC_IP_RELATED
{
    GmacMmcRxIpcIntrMask           = 0x0200,    /* Maintains the mask for interrupt generated from rx IPC statistic counters */
    GmacMmcRxIpcIntr               = 0x0208,    /* Maintains the interrupt that rx IPC statistic counters generate */

    GmacMmcRxIpV4FramesG           = 0x0210,    /* Good IPV4 datagrams received */
    GmacMmcRxIpV4HdrErrFrames      = 0x0214,    /* Number of IPV4 datagrams received with header errors */
    GmacMmcRxIpV4NoPayFrames       = 0x0218,    /* Number of IPV4 datagrams received which didnot have TCP/UDP/ICMP payload */
    GmacMmcRxIpV4FragFrames        = 0x021C,    /* Number of IPV4 datagrams received with fragmentation */
    GmacMmcRxIpV4UdpChkDsblFrames  = 0x0220,    /* Number of IPV4 datagrams received that had a UDP payload checksum disabled */

    GmacMmcRxIpV6FramesG           = 0x0224,    /* Good IPV6 datagrams received */
    GmacMmcRxIpV6HdrErrFrames      = 0x0228,    /* Number of IPV6 datagrams received with header errors */
    GmacMmcRxIpV6NoPayFrames       = 0x022C,    /* Number of IPV6 datagrams received which didnot have TCP/UDP/ICMP payload */

    GmacMmcRxUdpFramesG            = 0x0230,    /* Number of good IP datagrams with good UDP payload */
    GmacMmcRxUdpErrorFrames        = 0x0234,    /* Number of good IP datagrams with UDP payload having checksum error */

    GmacMmcRxTcpFramesG            = 0x0238,    /* Number of good IP datagrams with good TDP payload */
    GmacMmcRxTcpErrorFrames        = 0x023C,    /* Number of good IP datagrams with TCP payload having checksum error */

    GmacMmcRxIcmpFramesG           = 0x0240,    /* Number of good IP datagrams with good Icmp payload */
    GmacMmcRxIcmpErrorFrames       = 0x0244,    /* Number of good IP datagrams with Icmp payload having checksum error */

    GmacMmcRxIpV4OctetsG           = 0x0250,    /* Good IPV4 datagrams received excl. Ethernet hdr,FCS,Pad,Ip Pad bytes */
    GmacMmcRxIpV4HdrErrorOctets    = 0x0254,    /* Number of bytes in IPV4 datagram with header errors */
    GmacMmcRxIpV4NoPayOctets       = 0x0258,    /* Number of bytes in IPV4 datagram with no TCP/UDP/ICMP payload */
    GmacMmcRxIpV4FragOctets        = 0x025C,    /* Number of bytes received in fragmented IPV4 datagrams */
    GmacMmcRxIpV4UdpChkDsblOctets  = 0x0260,    /* Number of bytes received in UDP segment that had UDP checksum disabled */

    GmacMmcRxIpV6OctetsG           = 0x0264,    /* Good IPV6 datagrams received excl. Ethernet hdr,FCS,Pad,Ip Pad bytes */
    GmacMmcRxIpV6HdrErrorOctets    = 0x0268,    /* Number of bytes in IPV6 datagram with header errors */
    GmacMmcRxIpV6NoPayOctets       = 0x026C,    /* Number of bytes in IPV6 datagram with no TCP/UDP/ICMP payload */

    GmacMmcRxUdpOctetsG            = 0x0270,    /* Number of bytes in IP datagrams with good UDP payload */
    GmacMmcRxUdpErrorOctets        = 0x0274,    /* Number of bytes in IP datagrams with UDP payload having checksum error */

    GmacMmcRxTcpOctetsG            = 0x0278,    /* Number of bytes in IP datagrams with good TDP payload */
    GmacMmcRxTcpErrorOctets        = 0x027C,    /* Number of bytes in IP datagrams with TCP payload having checksum error */

    GmacMmcRxIcmpOctetsG           = 0x0280,    /* Number of bytes in IP datagrams with good Icmp payload */
    GmacMmcRxIcmpErrorOctets       = 0x0284,    /* Number of bytes in IP datagrams with Icmp payload having checksum error */
};

enum MMC_CNTRL_REG_BIT_DESCRIPTIONS
{
    GmacMmcCounterFreeze          = 0x00000008,        /* when set MMC counters freeze to current value */
    GmacMmcCounterResetOnRead     = 0x00000004,        /* when set MMC counters will be reset to 0 after read */
    GmacMmcCounterStopRollover    = 0x00000002,        /* when set counters will not rollover after max value */
    GmacMmcCounterReset           = 0x00000001,        /* when set all counters wil be reset (automatically cleared after 1 clk) */
};

enum MMC_RX_INTR_MASK_AND_STATUS_BIT_DESCRIPTIONS
{
    GmacMmcRxWDInt                = 0x00800000,        /* set when rxwatchdog error reaches half of max value */
    GmacMmcRxVlanInt              = 0x00400000,        /* set when GmacMmcRxVlanFramesGb counter reaches half of max value */
    GmacMmcRxFifoOverFlowInt      = 0x00200000,        /* set when GmacMmcRxFifoOverFlow counter reaches half of max value */
    GmacMmcRxPauseFrameInt        = 0x00100000,        /* set when GmacMmcRxPauseFrames counter reaches half of max value */
    GmacMmcRxOutOfRangeInt        = 0x00080000,        /* set when GmacMmcRxOutOfRangeType counter reaches half of max value */
    GmacMmcRxLengthErrorInt       = 0x00040000,        /* set when GmacMmcRxLengthError counter reaches half of max value */
    GmacMmcRxUcFramesInt          = 0x00020000,        /* set when GmacMmcRxUcFramesG counter reaches half of max value */
    GmacMmcRx1024OctInt           = 0x00010000,        /* set when GmacMmcRx1024ToMaxOctetsGb counter reaches half of max value */
    GmacMmcRx512OctInt            = 0x00008000,        /* set when GmacMmcRx512To1023OctetsGb counter reaches half of max value */
    GmacMmcRx256OctInt            = 0x00004000,        /* set when GmacMmcRx256To511OctetsGb counter reaches half of max value */
    GmacMmcRx128OctInt            = 0x00002000,        /* set when GmacMmcRx128To255OctetsGb counter reaches half of max value */
    GmacMmcRx65OctInt             = 0x00001000,        /* set when GmacMmcRx65To127OctetsG counter reaches half of max value */
    GmacMmcRx64OctInt             = 0x00000800,        /* set when GmacMmcRx64OctetsGb counter reaches half of max value */
    GmacMmcRxOverSizeInt          = 0x00000400,        /* set when GmacMmcRxOverSizeG counter reaches half of max value */
    GmacMmcRxUnderSizeInt         = 0x00000200,        /* set when GmacMmcRxUnderSizeG counter reaches half of max value */
    GmacMmcRxJabberErrorInt       = 0x00000100,        /* set when GmacMmcRxJabberError counter reaches half of max value */
    GmacMmcRxRuntErrorInt         = 0x00000080,        /* set when GmacMmcRxRuntError counter reaches half of max value */
    GmacMmcRxAlignErrorInt        = 0x00000040,        /* set when GmacMmcRxAlignError counter reaches half of max value */
    GmacMmcRxCrcErrorInt          = 0x00000020,        /* set when GmacMmcRxCrcError counter reaches half of max value */
    GmacMmcRxMcFramesInt          = 0x00000010,        /* set when GmacMmcRxMcFramesG counter reaches half of max value */
    GmacMmcRxBcFramesInt          = 0x00000008,        /* set when GmacMmcRxBcFramesG counter reaches half of max value */
    GmacMmcRxOctetGInt            = 0x00000004,        /* set when GmacMmcRxOctetCountG counter reaches half of max value */
    GmacMmcRxOctetGbInt           = 0x00000002,        /* set when GmacMmcRxOctetCountGb counter reaches half of max value */
    GmacMmcRxFrameInt             = 0x00000001,        /* set when GmacMmcRxFrameCountGb counter reaches half of max value */
};

enum MMC_TX_INTR_MASK_AND_STATUS_BIT_DESCRIPTIONS
{
    GmacMmcTxVlanInt              = 0x01000000,        /* set when GmacMmcTxVlanFramesG counter reaches half of max value */
    GmacMmcTxPauseFrameInt        = 0x00800000,        /* set when GmacMmcTxPauseFrames counter reaches half of max value */
    GmacMmcTxExessDefInt          = 0x00400000,        /* set when GmacMmcTxExessDef counter reaches half of max value */
    GmacMmcTxFrameInt             = 0x00200000,        /* set when GmacMmcTxFrameCount counter reaches half of max value */
    GmacMmcTxOctetInt             = 0x00100000,        /* set when GmacMmcTxOctetCountG counter reaches half of max value */
    GmacMmcTxCarrierErrorInt      = 0x00080000,        /* set when GmacMmcTxCarrierError counter reaches half of max value */
    GmacMmcTxExessColInt          = 0x00040000,        /* set when GmacMmcTxExessCol counter reaches half of max value */
    GmacMmcTxLateColInt           = 0x00020000,        /* set when GmacMmcTxLateCol counter reaches half of max value */
    GmacMmcTxDeferredInt          = 0x00010000,        /* set when GmacMmcTxDeferred counter reaches half of max value */
    GmacMmcTxMultiColInt          = 0x00008000,        /* set when GmacMmcTxMultiColG counter reaches half of max value */
    GmacMmcTxSingleCol            = 0x00004000,        /* set when GmacMmcTxSingleColG counter reaches half of max value */
    GmacMmcTxUnderFlowErrorInt    = 0x00002000,        /* set when GmacMmcTxUnderFlowError counter reaches half of max value */
    GmacMmcTxBcFramesGbInt        = 0x00001000,        /* set when GmacMmcTxBcFramesGb counter reaches half of max value */
    GmacMmcTxMcFramesGbInt        = 0x00000800,        /* set when GmacMmcTxMcFramesGb counter reaches half of max value */
    GmacMmcTxUcFramesInt          = 0x00000400,        /* set when GmacMmcTxUcFramesGb counter reaches half of max value */
    GmacMmcTx1024OctInt           = 0x00000200,        /* set when GmacMmcTx1024ToMaxOctetsGb counter reaches half of max value */
    GmacMmcTx512OctInt            = 0x00000100,        /* set when GmacMmcTx512To1023OctetsGb counter reaches half of max value */
    GmacMmcTx256OctInt            = 0x00000080,        /* set when GmacMmcTx256To511OctetsGb counter reaches half of max value */
    GmacMmcTx128OctInt            = 0x00000040,        /* set when GmacMmcTx128To255OctetsGb counter reaches half of max value */
    GmacMmcTx65OctInt             = 0x00000020,        /* set when GmacMmcTx65To127OctetsGb counter reaches half of max value */
    GmacMmcTx64OctInt             = 0x00000010,        /* set when GmacMmcTx64OctetsGb counter reaches half of max value */
    GmacMmcTxMcFramesInt          = 0x00000008,        /* set when GmacMmcTxMcFramesG counter reaches half of max value */
    GmacMmcTxBcFramesInt          = 0x00000004,        /* set when GmacMmcTxBcFramesG counter reaches half of max value */
    GmacMmcTxFrameGbInt           = 0x00000002,        /* set when GmacMmcTxFrameCountGb counter reaches half of max value */
    GmacMmcTxOctetGbInt           = 0x00000001,        /* set when GmacMmcTxOctetCountGb counter reaches half of max value */
};

enum InitialRegisters
{
    DmaIntEnable           = DmaIntNormal      | // 16
                             DmaIntAbnormal    | // 15
                             DmaIntBusError    | // 13
                             DmaIntRxStopped   | // 8
                             DmaIntRxNoBuffer  | // 7
                             DmaIntRxCompleted | // 6
                             DmaIntTxUnderflow  | // 5
                             DmaIntRcvOverflow  | // 4
                             DmaIntTxNoBuffer  | // 2
                             DmaIntTxStopped   | // 1
                             DmaIntTxCompleted,  // 0

    DmaIntDisable          = 0,
};



uint32_t eth_mac_read_reg(uint64_t base, uint32_t offset);

void eth_mac_write_reg(uint64_t base, uint32_t offset, uint32_t data);

void eth_mac_set_bits(uint64_t base, uint32_t offset, uint32_t pos);

void eth_mac_clear_bits(uint64_t base, uint32_t offset, uint32_t pos);

uint16_t eth_mdio_read(uint64_t regbase, uint32_t phybase, uint32_t offset);

void eth_mdio_write(uint64_t regbase, uint32_t phybase, uint32_t offset, uint16_t data);

void eth_mac_set_addr(struct net_device *gmacdev, uint8_t *addr);

void eth_gmac_get_mac_addr(struct net_device *gmacdev, uint8_t *addr);

void eth_dma_reset(struct net_device *gmacdev);

void eth_gmac_resume_dma_rx(struct net_device *gmacdev);

void eth_gmac_resume_dma_tx(struct net_device *gmacdev);

void eth_dma_enable_rx(struct net_device *gmacdev);

void eth_dma_enable_tx(struct net_device *gmacdev);

void eth_gmac_disable_dma_tx(struct net_device *gmacdev);

void eth_gmac_disable_dma_rx(struct net_device *gmacdev);

void eth_gmac_enable_rx(struct net_device *gmacdev);

void eth_gmac_enable_tx(struct net_device *gmacdev);

void eth_gmac_disable_rx(struct net_device *gmacdev);

void eth_gmac_disable_tx(struct net_device *gmacdev);

void eth_dma_clear_curr_irq(struct net_device *gmacdev);

void eth_dma_clear_irq(struct net_device *gmacdev, uint32_t value);

void eth_dma_enable_interrupt(struct net_device *gmacdev, uint32_t value);

void eth_dma_disable_interrupt_all(struct net_device *gmacdev);

void eth_dma_disable_interrupt(struct net_device *gmacdev, uint32_t value);

void eth_gmac_disable_mmc_irq(struct net_device *gmacdev);

void eth_dma_bus_mode_init(struct net_device *gmacdev);

void eth_dma_control_init(struct net_device *gmacdev);

void eth_dma_axi_bus_mode_init(struct net_device *gmacdev);

void eth_dma_reg_init(struct net_device *gmacdev);

void eth_gmac_back_off_limit(struct net_device *gmacdev, uint32_t value);

void eth_gmac_config_init(struct net_device *gmacdev);

void eth_gmac_set_pass_control(struct net_device *gmacdev, uint32_t value);

void eth_gmac_frame_filter(struct net_device *gmacdev);

void eth_gmac_flow_control(struct net_device *gmacdev);

void eth_gmac_reg_init(struct net_device *gmacdev);

void eth_setup_tx_desc_queue(struct net_device *gmacdev, uint32_t desc_num);

void eth_setup_rx_desc_queue(struct net_device *gmacdev, uint32_t desc_num);

bool eth_get_desc_owner(DmaDesc *desc);

uint32_t eth_get_rx_length(DmaDesc *desc);

bool eth_is_tx_desc_valid(DmaDesc *desc);

bool eth_is_desc_empty(DmaDesc *desc);

bool eth_is_rx_desc_valid(DmaDesc *desc);

bool eth_is_last_rx_desc(DmaDesc *desc);

bool eth_is_last_tx_desc(DmaDesc *desc);

// void eth_mac_get_hw_feature(struct net_device *gmacdev);


#endif // __LS2K_ETH_DEV_H__
