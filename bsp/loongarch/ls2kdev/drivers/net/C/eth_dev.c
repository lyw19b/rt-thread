#include "eth_platform.h"
#include "eth_dev.h"

// read mac register
uint32_t eth_mac_read_reg(uint64_t base, uint32_t offset)
{
    uint64_t addr;
    uint32_t data;

    addr = base + (uint32_t)offset;
    data = *(volatile uint32_t *)addr;
    return data;
}

// write mac register
void eth_mac_write_reg(uint64_t base, uint32_t offset, uint32_t data)
{
    uint64_t addr;
    addr = base + (uint32_t)offset;
    *(volatile uint32_t *)addr = data;
}

// set bits in mac register
void eth_mac_set_bits(uint64_t base, uint32_t offset, uint32_t pos)
{
    uint32_t data;
    data = eth_mac_read_reg(base, offset);
    data |= pos;
    eth_mac_write_reg(base, offset, data);
}

// clear bits in mac register
void eth_mac_clear_bits(uint64_t base, uint32_t offset, uint32_t pos)
{
    uint32_t data;
    data = eth_mac_read_reg(base, offset);
    data &= (~pos);
    eth_mac_write_reg(base, offset, data);
}

// mdio read phy register
uint16_t eth_mdio_read(uint64_t regbase, uint32_t phybase, uint32_t offset)
{
    // write addr
    uint32_t addr;
    addr = ((phybase << GmiiDevShift) & GmiiDevMask) | ((offset << GmiiRegShift) & GmiiRegMask);
    addr |= GmiiCsrClk4 | GmiiBusy;
    eth_mac_write_reg(regbase, GmacGmiiAddr, addr);

    // waiting for completed
    while (eth_mac_read_reg(regbase, GmacGmiiAddr) & GmiiBusy);

    // read data
    return (uint16_t)(eth_mac_read_reg(regbase, GmacGmiiData) & 0xFFFF);
}

// mdio write phy register
void eth_mdio_write(uint64_t regbase, uint32_t phybase, uint32_t offset, uint16_t data)
{
    // write data
    eth_mac_write_reg(regbase, GmacGmiiData, data);

    // write addr
    uint32_t addr;
    addr = ((phybase << GmiiDevShift) & GmiiDevMask) | ((offset << GmiiRegShift) & GmiiRegMask);
    addr |= GmiiWrite | GmiiCsrClk4 | GmiiBusy;
    eth_mac_write_reg(regbase, GmacGmiiAddr, addr);

    // waiting for completed
    while (eth_mac_read_reg(regbase, GmacGmiiAddr) & GmiiBusy);
}

// set mac address
void eth_mac_set_addr(struct net_device *gmacdev, uint8_t *addr)
{
    uint32_t data;
    data = (addr[5] << 8) | addr[4];
    eth_mac_write_reg(gmacdev->MacBase, GmacAddr0High, data);
    data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
    eth_mac_write_reg(gmacdev->MacBase, GmacAddr0Low, data);
}

// get mac address
void eth_gmac_get_mac_addr(struct net_device *gmacdev, uint8_t *addr)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacAddr0High);
    addr[5] = (data >> 8) & 0xff;
    addr[4] = (data)      & 0xff;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacAddr0Low);
    addr[3] = (data >> 24) & 0xff;
    addr[2] = (data >> 16) & 0xff;
    addr[1] = (data >> 8)  & 0xff;
    addr[0] = (data)       & 0xff;
}

// reset dma controller (all mac subsystem)
void eth_dma_reset(struct net_device *gmacdev)
{
    uint32_t data = 0;
    uint32_t i = 0, timeout = 200;

    // enable reset
    eth_mac_write_reg(gmacdev->DmaBase, DmaBusMode, DmaResetOn);

    // waiting for reset done
    do
    {
        volatile uint32_t count = 1000000;
        while (count --);
        data = eth_mac_read_reg(gmacdev->DmaBase, DmaBusMode);
    }
    while ((data & 1) && (++ i < timeout));

    // maybe timeout
    if (i >= timeout)
        eth_printf("gmac dma reset timeout\n");
}

// wakeup dma rx from suspend state
// to wait for new frame and fetch new descriptor
void eth_gmac_resume_dma_rx(struct net_device *gmacdev)
{
    eth_mac_write_reg(gmacdev->DmaBase, DmaRxPollDemand, 0);
}

// wakeup dma tx from suspend state
// to check for new descriptor
void eth_gmac_resume_dma_tx(struct net_device *gmacdev)
{
    eth_mac_write_reg(gmacdev->DmaBase, DmaTxPollDemand, 0);
}

// enable dma rx
void eth_dma_enable_rx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->DmaBase, DmaControl);
    data |= DmaRxStart;
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, data);
}

// enable dma tx
void eth_dma_enable_tx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->DmaBase, DmaControl);
    data |= DmaTxStart;
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, data);
}

// disable dma tx
void eth_gmac_disable_dma_tx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->DmaBase, DmaControl);
    data &= (~DmaTxStart);
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, data);
}

// disable dma rx
void eth_gmac_disable_dma_rx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->DmaBase, DmaControl);
    data &= (~DmaRxStart);
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, data);
}

// enable gmac-phy rx
void eth_gmac_enable_rx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacConfig);
    data |= GmacRx;
    eth_mac_write_reg(gmacdev->MacBase, GmacConfig, data);
}

// enable gmac-phy tx
void eth_gmac_enable_tx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacConfig);
    data |= GmacTx;
    eth_mac_write_reg(gmacdev->MacBase, GmacConfig, data);
}

// disable gmac-phy rx
void eth_gmac_disable_rx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacConfig);
    data &= (~GmacRx);
    eth_mac_write_reg(gmacdev->MacBase, GmacConfig, data);
}

// disable gmac-phy tx
void eth_gmac_disable_tx(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacConfig);
    data &= (~GmacTx);
    eth_mac_write_reg(gmacdev->MacBase, GmacConfig, data);
}

// clear all the pending interrupts
void eth_dma_clear_curr_irq(struct net_device *gmacdev)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->DmaBase, DmaStatus);
    eth_mac_write_reg(gmacdev->DmaBase, DmaStatus, data);
}

// clear selected interrupts
void eth_dma_clear_irq(struct net_device *gmacdev, uint32_t value)
{
    eth_mac_write_reg(gmacdev->DmaBase, DmaStatus, value);
}

// enable selected interrupts
void eth_dma_enable_interrupt(struct net_device *gmacdev, uint32_t value)
{
    eth_mac_write_reg(gmacdev->DmaBase, DmaInterrupt, value);
}

// disable all interrupts
void eth_dma_disable_interrupt_all(struct net_device *gmacdev)
{
    eth_mac_write_reg(gmacdev->DmaBase, DmaInterrupt, DmaIntDisable);
}

// disable selected interrupts
void eth_dma_disable_interrupt(struct net_device *gmacdev, uint32_t value)
{
    eth_mac_clear_bits(gmacdev->DmaBase, DmaInterrupt, value);
}

// disable all mmc interrupts
void eth_gmac_disable_mmc_irq(struct net_device *gmacdev)
{
    // disable mmc tx interrupt 0x0110
    eth_mac_write_reg(gmacdev->MacBase, GmacMmcIntrMaskTx, 0xFFFFFFFF);
    // disable mmc rx interrupt 0x010c
    eth_mac_write_reg(gmacdev->MacBase, GmacMmcIntrMaskRx, 0xFFFFFFFF);
    // disable mmc rx ipc interrupt 0x0200
    eth_mac_write_reg(gmacdev->MacBase, GmacMmcRxIpcIntrMask, 0xFFFFFFFF);
}

// init dma bus mode reg
void eth_dma_bus_mode_init(struct net_device *gmacdev)
{
    uint32_t value = 0;
    // enable mixed burst
    value |= DmaMixedBurstEnable;
    // enable 8xPBL mode and 32 burst length
    value |= DmaBurstLengthx8 | DmaBurstLength32;
    // enable 4 dwords desc and contiguous in ring mode
    value |= DmaDescriptor4DWords | DmaDescriptorSkip0;
    eth_mac_write_reg(gmacdev->DmaBase, DmaBusMode, value);
}

// init dma operation mode reg
void eth_dma_control_init(struct net_device *gmacdev)
{
    uint32_t value = 0;
    // enable RSF+TSF and OSF mode
    value |= DmaStoreAndForward | DmaTxSecondFrame;
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, value);
}

void eth_dma_axi_bus_mode_init(struct net_device *gmacdev)
{
    // disable lpi and remote wake up
    uint32_t value = 0;
    // maximize burst length between 4 to 256
    value |= 0xFF;
    // maximize outstanding requests
    value |= (0x77 << 16);
    eth_mac_write_reg(gmacdev->DmaBase, 0x0028, value);
}

// init dma register
void eth_dma_reg_init(struct net_device *gmacdev)
{
    // config bus mode
    eth_dma_bus_mode_init(gmacdev);

    // config operation mode
    eth_dma_control_init(gmacdev);

    // config axi bus mode
    eth_dma_axi_bus_mode_init(gmacdev);
}

// set back off limit
void eth_gmac_back_off_limit(struct net_device *gmacdev, uint32_t value)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacConfig);
    data &= (~GmacBackoffLimit);
    data |= value;
    eth_mac_write_reg(gmacdev->MacBase, GmacConfig, data);
}

// init gmac config reg
void eth_gmac_config_init(struct net_device *gmacdev)
{
    // try to get 1000M duplex
    eth_mac_set_bits(gmacdev->MacBase, GmacConfig, GmacTxConfig);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacWatchdog);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacJabber);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacFrameBurst);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacJumboFrame);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacRxOwn);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacLoopback);
    eth_mac_set_bits(gmacdev->MacBase, GmacConfig, GmacDuplex);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacRetry);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacPadCrcStrip);
    eth_mac_clear_bits(gmacdev->MacBase, GmacConfig, GmacDeferralCheck);

    // set back off limit
    eth_gmac_back_off_limit(gmacdev, GmacBackoffLimit0);
}

// set pass control forwarding
void eth_gmac_set_pass_control(struct net_device *gmacdev, uint32_t value)
{
    uint32_t data;
    data = eth_mac_read_reg(gmacdev->MacBase, GmacFrameFilter);
    data &= (~GmacPassControl);
    data |= value;
    eth_mac_write_reg(gmacdev->MacBase, GmacFrameFilter, data);
}

// config frame filter
void eth_gmac_frame_filter(struct net_device *gmacdev)
{
    // disable source address filter 9
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacSrcAddrFilter);
    // set pass control 7:6
    eth_gmac_set_pass_control(gmacdev, GmacPassControl0);
    // enable broadcast frame 5
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacBroadcast);
    // disable multicast frame 4
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacMulticastFilter);
    // enable normal destination address filter 3
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacDestAddrFilterInv);
    // disable multicast hash filter 2
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacMcastHashFilter);
    // disable unicast hash filter 1
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacUcastHashFilter);
    // disable promiscous mode 0
    eth_mac_clear_bits(gmacdev->MacBase, GmacFrameFilter, GmacPromiscuousMode);

    // enable frame filter 31
    eth_mac_set_bits(gmacdev->MacBase, GmacFrameFilter, GmacFilter);
}

// disable pause flow control in full-duplex
// disable backpressure in half-duplex
void eth_gmac_flow_control(struct net_device *gmacdev)
{
    uint32_t dma_ctrl;
    dma_ctrl = eth_mac_read_reg(gmacdev->DmaBase, DmaControl);
    // clear RFA and RFD
    dma_ctrl &= ~(DmaRxFlowCtrlAct | DmaRxFlowCtrlDeact);
    // disable EFC (hw flow control)
    dma_ctrl &= ~DmaEnHwFlowCtrl;
    eth_mac_write_reg(gmacdev->DmaBase, DmaControl, dma_ctrl);

    uint32_t flow_ctrl = 0;
    // set pause time to 0xFFFF
    flow_ctrl |= (0xFFFF << GmacPauseTimeShift);
    // disable rx/tx flow control
    flow_ctrl &= ~(GmacRxFlowControl | GmacTxFlowControl);
    eth_mac_write_reg(gmacdev->MacBase, GmacFlowControl, flow_ctrl);
}

// init gmac register
void eth_gmac_reg_init(struct net_device *gmacdev)
{
    // gmac config register
    eth_gmac_config_init(gmacdev);

    // config frame filter
    eth_gmac_frame_filter(gmacdev);

    // config flow control
    eth_gmac_flow_control(gmacdev);
}

// set up tx descriptor queue in ring mode
void eth_setup_tx_desc_queue(struct net_device *gmacdev, uint32_t desc_num)
{
    DmaDesc *desc; // cached desc addr
    uint32_t dma_addr; // physical desc addr
    void *buffer; // buffer addr

    desc = eth_malloc_align(sizeof(DmaDesc) * desc_num, 16);
    dma_addr = eth_virt_to_phys((uint64_t)desc);

    gmacdev->TxNext = 0;
    gmacdev->TxBusy = 0;

    // set desc list addr
    eth_mac_write_reg(gmacdev->DmaBase, DmaTxBaseAddr, dma_addr);

    for (int i = 0; i < desc_num; i ++, desc ++)
    {
        // allocate tx buffer
        buffer = eth_malloc_align(TX_BUF_SIZE, 16);

        // init desc
        gmacdev->TxDesc[i] = desc;
        gmacdev->TxBuffer[i] = buffer;

        desc->status = (i == desc_num - 1) ? TxDescEndOfRing : 0;
        desc->length = 0;
        desc->buffer1 = 0;
        desc->buffer2 = 0;
    }
}

// set up rx descriptor queue in ring mode
void eth_setup_rx_desc_queue(struct net_device *gmacdev, uint32_t desc_num)
{
    DmaDesc *desc; // cached desc addr
    uint32_t dma_addr; // physical desc addr
    void *buffer; // buffer addr

    desc = eth_malloc_align(sizeof(DmaDesc) * desc_num, 16);
    dma_addr = eth_virt_to_phys((uint64_t)desc);

    gmacdev->RxBusy = 0;

    // set desc list addr
    eth_mac_write_reg(gmacdev->DmaBase, DmaRxBaseAddr, dma_addr);

    for (int i = 0; i < desc_num; i ++, desc ++)
    {
        // allocate rx buffer
        buffer = eth_malloc_align(RX_BUF_SIZE, 16);
        // trans virtual addr to physical addr
        dma_addr = eth_virt_to_phys((uint64_t)buffer);

        // init desc
        gmacdev->RxDesc[i] = desc;
        gmacdev->RxBuffer[i] = buffer;

        desc->status = DescOwnByDma;
        desc->length = (i == desc_num - 1) ? RxDescEndOfRing : 0;
        desc->length |= ((RX_BUF_SIZE << DescSize1Shift) & DescSize1Mask);
        desc->buffer1 = dma_addr;
        desc->buffer2 = 0;
    }
}

bool eth_get_desc_owner(DmaDesc *desc)
{
    return ((desc->status & DescOwnByDma) == DescOwnByDma);
}

uint32_t eth_get_rx_length(DmaDesc *desc)
{
    return ((desc->status & DescFrameLengthMask) >> DescFrameLengthShift);
}

bool eth_is_tx_desc_valid(DmaDesc *desc)
{
    return ((desc->status & DescError) == 0);
}

bool eth_is_desc_empty(DmaDesc *desc)
{
    return (((desc->length & DescSize1Mask) == 0) && ((desc->length & DescSize2Mask) == 0));
}

bool eth_is_rx_desc_valid(DmaDesc *desc)
{
    uint32_t status = desc->status;
    return ((status & DescError) == 0) && ((status & DescRxFirst) == DescRxFirst) && ((status & DescRxLast) == DescRxLast);
}

bool eth_is_last_rx_desc(DmaDesc *desc)
{
    return ((desc->length & RxDescEndOfRing) == RxDescEndOfRing);
}

bool eth_is_last_tx_desc(DmaDesc *desc)
{
    return ((desc->status & TxDescEndOfRing) == TxDescEndOfRing);
}

// get dma hw feature
/*
void eth_mac_get_hw_feature(struct net_device *gmacdev)
{
    struct DmaFeature *dma_cap;
    dma_cap = &gmacdev->DmaCap;

    uint32_t hw_cap;
    hw_cap = eth_mac_read_reg(gmacdev->DmaBase, DmaHWFeature);

    dma_cap->mbps_10_100 = (hw_cap & DMA_HW_FEAT_MIISEL) >> 0;
    dma_cap->mbps_1000 = (hw_cap & DMA_HW_FEAT_GMIISEL) >> 1;
    dma_cap->half_duplex = (hw_cap & DMA_HW_FEAT_HDSEL) >> 2;
    dma_cap->hash_filter = (hw_cap & DMA_HW_FEAT_HASHSEL) >> 4;
    dma_cap->multi_addr = (hw_cap & DMA_HW_FEAT_ADDMAC) >> 5;
    // pcs - physical coding sublayer
    // only used for TBI/RTBI/SGMII phy interface
    dma_cap->pcs = (hw_cap & DMA_HW_FEAT_PCSSEL) >> 6;
    // sma - station management agent
    // allow access to phy registers
    dma_cap->sma_mdio = (hw_cap & DMA_HW_FEAT_SMASEL) >> 8;
    dma_cap->pmt_remote_wake_up = (hw_cap & DMA_HW_FEAT_RWKSEL) >> 9;
    dma_cap->pmt_magic_frame = (hw_cap & DMA_HW_FEAT_MGKSEL) >> 10;
    dma_cap->rmon = (hw_cap & DMA_HW_FEAT_MMCSEL) >> 11;
    dma_cap->time_stamp = (hw_cap & DMA_HW_FEAT_TSVER1SEL) >> 12;
    dma_cap->atime_stamp = (hw_cap & DMA_HW_FEAT_TSVER2SEL) >> 13;
    dma_cap->eee = (hw_cap & DMA_HW_FEAT_EEESEL) >> 14;
    dma_cap->av = (hw_cap & DMA_HW_FEAT_AVSEL) >> 15;
    dma_cap->tx_coe = (hw_cap & DMA_HW_FEAT_TXCOESEL) >> 16;
    dma_cap->rx_coe_type1 = (hw_cap & DMA_HW_FEAT_RXTYP1COE) >> 17;
    dma_cap->rx_coe_type2 = (hw_cap & DMA_HW_FEAT_RXTYP2COE) >> 18;
    dma_cap->rxfifo_over_2048 = (hw_cap & DMA_HW_FEAT_RXFIFOSIZE) >> 19;
    dma_cap->number_rx_channel = (hw_cap & DMA_HW_FEAT_RXCHCNT) >> 20;
    dma_cap->number_tx_channel = (hw_cap & DMA_HW_FEAT_TXCHCNT) >> 22;
    dma_cap->enh_desc = (hw_cap & DMA_HW_FEAT_ENHDESSEL) >> 24;
}
*/
