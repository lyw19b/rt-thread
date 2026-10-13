#include "eth_dev.h"
#include "eth_platform.h"

// check RGMII phy status
void eth_phy_rgsmii_check(struct net_device *gmacdev)
{
    // use mac intf to check phy instead of mii
    uint32_t value, status;
    value = eth_mac_read_reg(gmacdev->MacBase, GmacRgsmiiStatus);
    status = (value & MacLinkStatus >> MacLinkStatusOff);

    // some OS require updating link status
    if (gmacdev->LinkStatus != status)
        eth_update_linkstate(gmacdev, status);

    // check the link status
    if (status) {
        // link is up
        gmacdev->LinkStatus = 1;
        gmacdev->DuplexMode = value & MacLinkMode;

        uint32_t speed = value & MacLinkSpeed;
        if (speed == MacLinkSpeed_125)
            gmacdev->Speed = 1000;
        else if (speed == MacLinkSpeed_25)
            gmacdev->Speed = 100;
        else
            gmacdev->Speed = 10;

        eth_printf("Link is Up - %u Mpbs / %s Duplex\n", gmacdev->Speed,
            gmacdev->DuplexMode ? "Full" : "Half");
    } else {
        // link is down
        gmacdev->LinkStatus = 0;
        eth_printf("Link is Down\n");
    }
}

// init phy
// todo 初始化+phy实现rgmii的自动协商
void eth_phy_init(struct net_device *gmacdev)
{
    uint32_t phy = 0;
    uint16_t data;

    // read phy id
    data = eth_mdio_read(gmacdev->MacBase, gmacdev->PhyBase, 2);
    phy |= (data << 16);
    data = eth_mdio_read(gmacdev->MacBase, gmacdev->PhyBase, 3);
    phy |= data;

    switch (phy) {
    case 0x001cc915:
        eth_printf("probed ethernet phy RTL8211E, id 0x%08x\n", phy);
        break;

    case 0x0000010a:
        eth_printf("probed ethernet phy YT8511H/C, id 0x%08x\n", phy);
        break;

    default:
        eth_printf("probed unknown ethernet phy, id 0x%08x\n", phy);
        break;
    }
}

int eth_tx(struct net_device *gmacdev, uint64_t pbuf)
{
    void *buffer;
    uint32_t length;

    uint32_t dma_addr;
    uint32_t desc_idx = gmacdev->TxNext;
    DmaDesc *txdesc = gmacdev->TxDesc[desc_idx];
    uint32_t is_last = eth_is_last_tx_desc(txdesc);

    // 如果desc由dma持有，说明满了
    if (eth_get_desc_owner(txdesc))
    {
        // eth_printf("[eth_tx] tx desc is full\n");
        return -1;
    }

    buffer = gmacdev->TxBuffer[desc_idx];
    length = eth_handle_tx_buffer(pbuf, buffer);
    dma_addr = eth_virt_to_phys((uint64_t)buffer);

    // set desc
    txdesc->status |= (DescOwnByDma | DescTxIntEnable | DescTxLast | DescTxFirst);
    txdesc->length = ((length << DescSize1Shift) & DescSize1Mask);
    txdesc->buffer1 = dma_addr;
    txdesc->buffer2 = 0;

    gmacdev->TxNext = is_last ? 0 : (desc_idx + 1);

    eth_sync_dcache();

    // start tx
    eth_gmac_resume_dma_tx(gmacdev);

    return 0;
}

uint64_t eth_rx(struct net_device *gmacdev)
{
    uint32_t desc_idx = gmacdev->RxBusy;
    DmaDesc *rxdesc = gmacdev->RxDesc[desc_idx];
    uint32_t is_last = eth_is_last_rx_desc(rxdesc);

    // 如果desc为空或仍由DMA持有，则表示没有新数据包
    // 恢复rx中断并退出
    if (eth_is_desc_empty(rxdesc) || eth_get_desc_owner(rxdesc))
    {
        // eth_printf("[eth_rx] no rx desc available\n");
        eth_dma_enable_interrupt(gmacdev, DmaIntEnable);
        return 0;
    }

    void *pbuf = 0;
    uint32_t dma_addr = rxdesc->buffer1;

    // handle received packet
    if (eth_is_rx_desc_valid(rxdesc))
    {
        uint32_t length = eth_get_rx_length(rxdesc);
        void *buffer = (void *)eth_phys_to_virt(dma_addr);

        eth_sync_dcache();

        // 创建length长度的pbuf，将buffer拷贝到pbuf中
        // 或者实现zero-copy rx
        pbuf = eth_handle_rx_buffer(buffer, length);

        gmacdev->rx_bytes += length;
        gmacdev->rx_packets ++;
    }
    else
    {
        gmacdev->rx_errors ++;
    }

    // set desc
    rxdesc->status = DescOwnByDma;
    rxdesc->length = is_last ? RxDescEndOfRing : 0;
    rxdesc->length |= ((RX_BUF_SIZE << DescSize1Shift) & DescSize1Mask);
    rxdesc->buffer1 = dma_addr;
    rxdesc->buffer2 = 0;

    gmacdev->RxBusy = is_last ? 0 : (desc_idx + 1);

    return pbuf;
}

// handle all transmitted packet
void eth_handle_tx_over(struct net_device *gmacdev)
{
    while (1)
    {
        uint32_t desc_idx = gmacdev->TxBusy;
        DmaDesc *txdesc = gmacdev->TxDesc[desc_idx];

        // 检查desc是否仍由DMA持有，或者是否为空
        // 可能没有发送完成，或者没有新的发送desc
        if (eth_get_desc_owner(txdesc) || eth_is_desc_empty(txdesc))
        {
            break;
        }

        if (eth_is_tx_desc_valid(txdesc))
        {
            uint32_t length = (txdesc->length & DescSize1Mask) >> DescSize1Shift;
            gmacdev->tx_bytes += length;
            gmacdev->tx_packets ++;
        }
        else
        {
            gmacdev->tx_errors ++;
        }

        uint32_t is_last = eth_is_last_tx_desc(txdesc);

        // clear desc
        txdesc->status = is_last ? TxDescEndOfRing : 0;
        txdesc->length = 0;
        txdesc->buffer1 = 0;
        txdesc->buffer2 = 0;

        gmacdev->TxBusy = is_last ? 0 : (desc_idx + 1);
    }
}

// isr
// gmac + dma irqs
// check gmac first, then dma
void eth_irq(struct net_device *gmacdev)
{
    uint32_t dma_status;
    uint32_t dma_int_enable = DmaIntEnable;

    dma_status = eth_mac_read_reg(gmacdev->DmaBase, DmaStatus);
    if (dma_status == 0)
        return;

    // disable interrupt
    eth_dma_disable_interrupt_all(gmacdev);

    /// check gmac interrupt

    // 29 time-stamp trigger interrupt
    // time-stamp not enabled for now

    // 28 gmac pmt interrupt
    if (dma_status & GmacPmtIntr)
    {
        eth_printf("gmac pmt interrupt\n");
    }

    // 27 gmac mmc interrupt
    if (dma_status & GmacMmcIntr)
    {
        eth_printf("gmac mmc interrupt\n");
    }

    // 26 gmac line interface interrupt
    // PCS or RGMII
    if (dma_status & GmacLineIntfIntr)
    {
        // eth_printf("gmac line interface interrupt\n");

        // PCS interrupt
        eth_mac_read_reg(gmacdev->MacBase, GmacInterruptStatus);
        eth_mac_read_reg(gmacdev->MacBase, GmacInterruptMask);

        // RGMII interrupt
        if (eth_mac_read_reg(gmacdev->MacBase, GmacInterruptStatus) & GmacRgmiiIntSts)
        {
            eth_mac_read_reg(gmacdev->MacBase, GmacRgsmiiStatus);
        }

        eth_phy_rgsmii_check(gmacdev);
    }

    /// check dma interrupt
    // classified into "normal" / "abnormal"
    // read DmaIntNormal or/and DmaIntAbnormal

    // clear interrupt
    eth_mac_write_reg(gmacdev->DmaBase, DmaStatus, dma_status);

    // 14 early receive interrupt
    // (dma have filled the first buffer of packet)
    // normal

    // 13 fatal bus error interrupt
    // abnormal
    if (dma_status & DmaIntBusError)
    {
        eth_printf("gmac fatal bus error interrupt\n");
    }

    // 10 early transmit interrupt
    // frame to be transmitted fully transfered to the MTL transmit fifo
    // abnormal (quite normal actually)

    // 8 receive process stopped
    // abnormal
    if (dma_status & DmaIntRxStopped)
    {
        eth_printf("gmac receive process stopped\n");
        eth_dma_enable_rx(gmacdev);
    }

    // 7 receive buffer unavailable
    // abnormal
    if (dma_status & DmaIntRxNoBuffer)
    {
        eth_printf("gmac receive buffer unavailable\n");
        // try to recover
        dma_int_enable &= ~DmaIntRxNoBuffer;
        eth_gmac_resume_dma_rx(gmacdev);
        eth_rx_ready(gmacdev);
    }

    // 6 receive interrupt (reception completed)
    // normal
    if (dma_status & DmaIntRxCompleted)
    {
        //eth_printf("gmac dma rx normal\n");
        dma_int_enable &= ~DmaIntRxCompleted;
        eth_rx_ready(gmacdev);
    }

    // 5 transmit underflow
    // abnormal
    if (dma_status & DmaIntTxUnderflow)
    {
        eth_printf("gmac transmit underflow\n");
    }

    // 4 receive overflow
    // abnormal
    if (dma_status & DmaIntRcvOverflow)
    {
        eth_printf("gmac receive underflow\n");
    }

    // 2 transmit buffer unavailable
    // (host should change the ownership of the desc)
    // normal
    // if nothing to send, dma will also raise this
    if (dma_status & DmaIntTxNoBuffer)
    {
        // eth_printf("gmac transmit buffer unavailable\n");
    }

    // 1 transmit process stopped
    // abnormal
    if (dma_status & DmaIntTxStopped)
    {
        eth_printf("gmac transmit process stopped\n");
    }

    // 0 transmit interrupt (transmit completed)
    // normal
    if (dma_status & DmaIntTxCompleted)
    {
        // eth_printf("gmac dma tx normal\n");
        eth_handle_tx_over(gmacdev);
    }

    // enable interrupt
    eth_dma_enable_interrupt(gmacdev, dma_int_enable);
}

int eth_init(struct net_device *gmacdev)
{
    // set mac reg address
    gmacdev->MacBase = (gmacdev->iobase + MACBASE);
    gmacdev->DmaBase = (gmacdev->iobase + DMABASE);
    gmacdev->PhyBase = DEFAULT_PHY_BASE;

    // init all mac (gmac+dma) subsystem
    eth_dma_reset(gmacdev);

    // init mac address
    eth_mac_set_addr(gmacdev, gmacdev->MacAddr);

    // get mac hw feature
    // eth_mac_get_hw_feature(gmacdev);

    // read version
    gmacdev->Version = eth_mac_read_reg(gmacdev->MacBase, GmacVersion);

    // init phy
    eth_phy_init(gmacdev);

    // setup rx/tx desc_queue
    eth_setup_rx_desc_queue(gmacdev, RX_DESC_NUM);
    eth_setup_tx_desc_queue(gmacdev, TX_DESC_NUM);

    // init dma register
    eth_dma_reg_init(gmacdev);

    // init gmac register
    eth_gmac_reg_init(gmacdev);

    // sync all desc and config
    eth_sync_dcache();

    // disable all mmc interrupts
    eth_gmac_disable_mmc_irq(gmacdev);

    // clear all pending interrupts
    eth_dma_clear_curr_irq(gmacdev);

    // enable interrupt
    eth_dma_enable_interrupt(gmacdev, DmaIntEnable);

    // enable gmac-phy rx and tx
    eth_gmac_enable_rx(gmacdev);
    eth_gmac_enable_tx(gmacdev);

    // enable dma rx and tx
    eth_dma_enable_rx(gmacdev);
    eth_dma_enable_tx(gmacdev);

    // install isr
    eth_isr_install();

    return 0;
}
