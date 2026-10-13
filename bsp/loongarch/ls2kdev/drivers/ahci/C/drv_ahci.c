#include "libahci.h"
#include "libata.h"

// only for test
uint8_t sector_data[512];

uint32_t ahci_readl(uint64_t addr)
{
    return *((volatile uint32_t *)addr);
}

void ahci_writel(uint32_t data, uint64_t addr)
{
    *((volatile uint32_t *)addr) = data;
}

uint32_t ahci_ffs32(uint32_t i)
{
    uint32_t bit;

    if (0 == i)
        return 0;

    for (bit = 1; !(i & 1); ++ bit)
        i >>= 1;

    return bit;
}

uint64_t ata_strnlen(const char *s, uint64_t maxlen)
{
    const char *sc;
    for (sc = s; maxlen != 0 && *sc != '\0'; maxlen--, ++sc);
    return sc - s;
}

uint64_t ata_id_n_sectors(uint16_t *id)
{
    if (ata_id_has_lba(id)) {
        if (ata_id_has_lba48(id))
            return ata_id_u64(id, ATA_ID_LBA_CAPACITY_2);
        else
            return ata_id_u32(id, ATA_ID_LBA_CAPACITY);
    } else {
        return 0;
    }
}

void ata_id_string(const uint16_t *id, uint8_t *s, uint32_t ofs, uint32_t len)
{
    uint8_t c;

    while (len > 0) {
        c = id[ofs] >> 8;
        *s = c;
        s++;

        c = id[ofs] & 0xff;
        *s = c;
        s++;

        ofs++;
        len -= 2;
    }
}

void ata_id_c_string(const uint16_t *id, uint8_t *s, uint32_t ofs, uint32_t len)
{
    uint8_t *p;

    ata_id_string(id, s, ofs, len - 1);

    p = s + ata_strnlen((char *)s, len - 1);
    while (p > s && p[-1] == ' ')
        p--;
    *p = '\0';
}

void ahci_dump_buffer(void *p, int len)
{
    uint32_t i;
    uint8_t *q = p;

    ahci_printf("------ dump_buffer ------\n");
    ahci_printf("buf = %p, len = %d\n\n", q, len);

    for (i = 0; i < len; ++ i)
    {
        if (!(i & 0xF))
            ahci_printf("%p ", &q[i]);

        ahci_printf(" %02x", q[i]);

        if ((i & 0xF) == 0xF)
            ahci_printf("\n");
    }

    ahci_printf("\n----------------------\n");
}

// dump ahci info
void ahci_print_info(struct ahci_device *ahci_dev)
{
    uint64_t host_mmio = ahci_dev->mmio_base;
    uint32_t vers, cap, cap2, impl, speed;
    const char *speed_s;
    const char *scc_s;

    vers = ahci_dev->version;
    cap = ahci_dev->cap;
    cap2 = ahci_dev->cap2;
    impl = ahci_dev->port_map;

    speed = (cap >> 20) & 0xf;
    if (speed == 1)
        speed_s = "1.5";
    else if (speed == 2)
        speed_s = "3";
    else if (speed == 3)
        speed_s = "6";
    else
        speed_s = "?";

    scc_s = "SATA";

    ahci_printf("AHCI vers %02x%02x.%02x%02x, "
               "%u slots, %u ports, %s Gbps, 0x%x impl, %s mode\n",
               (vers >> 24) & 0xff,
               (vers >> 16) & 0xff,
               (vers >> 8) & 0xff,
               vers & 0xff,
               ((cap >> 8) & 0x1f) + 1,
               (cap & 0x1f) + 1,
               speed_s, impl, scc_s);

    ahci_printf("flags: %s%s%s%s%s%s%s%s%s%s%s%s%s%s"
		"%s%s%s%s%s%s%s%s%s\n",
		cap & HOST_CAP_64 ? "64bit " : "",
		cap & HOST_CAP_NCQ ? "ncq " : "",
		cap & HOST_CAP_SNTF ? "sntf " : "",
		cap & HOST_CAP_MPS ? "ilck " : "",
		cap & HOST_CAP_SSS ? "stag " : "",
		cap & HOST_CAP_ALPM ? "pm " : "",
		cap & HOST_CAP_LED ? "led " : "",
		cap & HOST_CAP_CLO ? "clo " : "",
		cap & HOST_CAP_ONLY ? "only " : "",
		cap & HOST_CAP_PMP ? "pmp " : "",
		cap & HOST_CAP_FBS ? "fbs " : "",
		cap & HOST_CAP_PIO_MULTI ? "pio " : "",
		cap & HOST_CAP_SSC ? "slum " : "",
		cap & HOST_CAP_PART ? "part " : "",
		cap & HOST_CAP_CCC ? "ccc " : "",
		cap & HOST_CAP_EMS ? "ems " : "",
		cap & HOST_CAP_SXS ? "sxs " : "",
		cap2 & HOST_CAP2_DESO ? "deso " : "",
		cap2 & HOST_CAP2_SADM ? "sadm " : "",
		cap2 & HOST_CAP2_SDS ? "sds " : "",
		cap2 & HOST_CAP2_APST ? "apst " : "",
		cap2 & HOST_CAP2_NVMHCI ? "nvmp " : "",
		cap2 & HOST_CAP2_BOH ? "boh " : ""
		);
}

void ahci_sata_print_info(struct ahci_blk_dev *pdev)
{
    ahci_printf("SATA Device Info:\n");
    ahci_printf("S/N: %s\n", pdev->serial);
    ahci_printf("Product model number: %s\n", pdev->product);
    ahci_printf("Firmware version: %s\n", pdev->revision);
    ahci_printf("Capacity: %lu sectors\n", pdev->lba);
}

// get base address of 'port'
uint64_t ahci_port_base(uint64_t base, uint8_t port)
{
    return base + 0x100 + (port * 0x80);
}

// init ahci
int ahci_host_init(struct ahci_device *ahci_dev)
{
    uint32_t tmp = 0;
    uint32_t timeout = 0;
    uint64_t port_mmio = 0;
    uint64_t host_mmio = ahci_dev->mmio_base;

    // reset ahci controller
    tmp = ahci_readl(host_mmio + HOST_CTL);
    if ((tmp & HOST_RESET) == 0)
        ahci_writel(tmp | HOST_RESET, host_mmio + HOST_CTL);
    // wait for reset done
    do
    {
        ahci_mdelay(1);
        tmp = ahci_readl(host_mmio + HOST_CTL);
    }
    while (tmp & HOST_RESET);

    // enable ahci
    tmp = ahci_readl(host_mmio + HOST_CTL);
    ahci_writel(tmp | HOST_AHCI_EN, host_mmio + HOST_CTL);
    ahci_mdelay(1);

    // init cap and pi
    // beware if no firmware initialized before
    // these bits are ready-only after write-once
    tmp = HOST_CAP_MPS | HOST_CAP_SSS;
    ahci_writel(tmp, host_mmio + HOST_CAP);
    ahci_writel(0xf, host_mmio + HOST_PORTS_IMPL);
    ahci_readl(host_mmio + HOST_PORTS_IMPL); // flush

    // get cap and cap2
    ahci_dev->cap = ahci_readl(host_mmio + HOST_CAP);
    ahci_dev->cap2 = ahci_readl(host_mmio + HOST_CAP2);

    // get version
    ahci_dev->version = ahci_readl(host_mmio + HOST_VERSION);

    // get which ports the ahci supports
    ahci_dev->port_map = ahci_readl(host_mmio + HOST_PORTS_IMPL);

    // get how many ports the ahci supports
    ahci_dev->n_ports = (ahci_dev->cap & 0x1f) + 1;

    // init each port
    // for ls2kla, only 1 port
    for (uint8_t i = 0; i < ahci_dev->n_ports; ++ i)
    {
        ahci_dev->port[i].port_mmio = ahci_port_base(host_mmio, i);
        port_mmio = ahci_dev->port[i].port_mmio;

        // ensure that the sata is in idle state
        tmp = ahci_readl(port_mmio + PORT_CMD);

        // check ST FRE FR CR bits
        if (tmp & (PORT_CMD_LIST_ON | PORT_CMD_FIS_ON |
                   PORT_CMD_FIS_RX | PORT_CMD_START))
        {
            // clear ST to stop
            tmp &= ~PORT_CMD_START;
            ahci_writel(tmp, port_mmio + PORT_CMD);

            // sleep 500 msecs
            ahci_mdelay(500);

            // wait for CR
            while (ahci_readl(port_mmio + PORT_CMD) & PORT_CMD_LIST_ON);
        }

        // set spin up
        tmp = ahci_readl(port_mmio + PORT_CMD);
        ahci_writel((tmp | PORT_CMD_SPIN_UP), port_mmio + PORT_CMD);

        // wait for spin up
        timeout = 1000;
        do
        {
            ahci_mdelay(1);
            tmp = ahci_readl(port_mmio + PORT_CMD);
        } while (!(tmp | PORT_CMD_SPIN_UP) && --timeout);
        if (timeout <= 0)
        {
            ahci_printf("spin up cannot finish\n");
            return -1;
        }

        // wait for port link up
        timeout = 1000;
        do
        {
            ahci_mdelay(1);
            tmp = ahci_readl(port_mmio + PORT_SCR_STAT);
            tmp &= 0x0f;
        } while (!(tmp == 0x3 || tmp == 0x1) && -- timeout);
        if (timeout <= 0)
            ahci_printf("port %u sata link timeout\n", i);
        else
            ahci_printf("port %u sata link up\n", i);

        // clear serr
        tmp = ahci_readl(port_mmio + PORT_SCR_ERR);
        ahci_writel(tmp, port_mmio + PORT_SCR_ERR);

        // ack any pending irq events for this port
        tmp = ahci_readl(host_mmio + PORT_IRQ_STAT);
        ahci_writel(tmp, host_mmio + PORT_IRQ_STAT);

        ahci_writel(1 << i, host_mmio + HOST_IRQ_STAT);

        // set irq mask
        ahci_writel(DEF_PORT_IRQ, port_mmio + PORT_IRQ_MASK);

        // detect port status
        tmp = ahci_readl(port_mmio + PORT_SCR_STAT);
        // port link up
        if ((tmp & 0x0fu) == 0x03)
            ahci_dev->port_map_linkup |= (0x01 << i);
    }

    // interrupt enable
    // we dont use interrupt actually
    tmp = ahci_readl(host_mmio + HOST_CTL);
    ahci_writel(tmp | HOST_IRQ_EN, host_mmio + HOST_CTL);
    tmp = ahci_readl(host_mmio + HOST_CTL);

    return 0;
}

// configure sgdma
uint32_t ahci_fill_sg(struct ahci_device *ahci_dev, uint8_t port,
                     uint8_t *buf, uint32_t buf_len)
{
    struct ahci_ioport *pp = &ahci_dev->port[port];
    struct ahci_sg *ahci_sg = pp->cmd_tbl_sg;
    uint32_t sg_count, max_bytes;

    max_bytes = AHCI_MAX_BYTES_PER_SG; // 4 MiB
    sg_count = ((buf_len - 1) / max_bytes) + 1;

    if (sg_count > AHCI_MAX_SG) // 56 * 4 MiB
    {
        ahci_printf("too much sg\n");
        return 0;
    }

    for (uint32_t i = 0; i < sg_count; ++ i)
    {
        uint64_t buf_dma = ahci_virt_to_phys((uint64_t)(buf + i * max_bytes));
        ahci_sg->addr_lo = (uint32_t)(buf_dma & 0xffffffff);
        ahci_sg->addr_hi = (uint32_t)(buf_dma >> 32);
        ahci_sg->flags_size = (0x3fffff & (buf_len < max_bytes ? (buf_len - 1) : (max_bytes - 1)));
        ahci_sg ++;
        buf_len -= max_bytes;
    }

    return sg_count;
}

// fill cmd slot
void ahci_fill_cmd_slot(struct ahci_ioport *pp, uint32_t cmd_slot, uint32_t opts)
{
    struct ahci_cmd_hdr *cmd_hdr = (pp->cmd_slot + AHCI_CMD_SZ * cmd_slot);

    cmd_hdr->opts = opts;
    cmd_hdr->status = 0;
    cmd_hdr->tbl_addr_lo = (uint32_t)(pp->cmd_tbl_dma & 0xffffffff);
    cmd_hdr->tbl_addr_hi = (uint32_t)(pp->cmd_tbl_dma >> 32);
}

uint32_t ahci_get_cmd_slot(uint32_t value)
{
    uint32_t slot = value ? ahci_ffs32(value) : 0;
    return slot;
}

// send ahci cmd
uint32_t ahci_exec_ata_cmd(struct ahci_device *ahci_dev, uint8_t port,
                      struct sata_fis_h2d *cfis, void *buf, uint32_t buf_len,
                      uint32_t is_write)
{
    struct ahci_ioport *pp = &ahci_dev->port[port];
    uint64_t port_mmio = pp->port_mmio;
    uint32_t opts;
    uint32_t sg_count = 0, cmd_slot = 0;

    // get available slot
    cmd_slot = ahci_get_cmd_slot(ahci_readl(port_mmio + PORT_CMD_ISSUE));

    if (cmd_slot == 32)
    {
        ahci_printf("cannot find empty command slot\n");
        return 0;
    }

    // check xfer length
    // 65536 * 512
    if (buf_len > AHCI_MAX_BYTES_PER_TRANS)
    {
        ahci_printf("max transfer length is %u bytes\n", AHCI_MAX_BYTES_PER_TRANS);
        return 0;
    }

    ahci_memcpy((void *)pp->cmd_tbl, cfis, sizeof(struct sata_fis_h2d));

    if (buf && buf_len)
        sg_count = ahci_fill_sg(ahci_dev, port, buf, buf_len);
    opts = (sizeof(struct sata_fis_h2d) >> 2) | (sg_count << 16) | (is_write << 6);

    ahci_fill_cmd_slot(pp, cmd_slot, opts);

    ahci_sync_dcache();

    // start transfer
    ahci_writel(1 << cmd_slot, port_mmio + PORT_CMD_ISSUE);

    // waiting for completion
    while (ahci_readl(port_mmio + PORT_CMD_ISSUE) & (1 << cmd_slot));

    ahci_sync_dcache();

    //ahci_printf("ahci_exec_ata_cmd: %d byte transferred.\n",
    //        pp->cmd_slot->status);

    return buf_len;
}

void ahci_set_feature(struct ahci_device *ahci_dev, uint8_t subcmd, uint8_t action)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D;
    cfis.pm_port_c = 0x80;
    cfis.command = ATA_CMD_SET_FEATURES;
    cfis.features = subcmd;
    cfis.sector_count = action;

    ahci_exec_ata_cmd(ahci_dev, port, &cfis, NULL, 0, READ_CMD);
}

// init port
int ahci_port_start(struct ahci_device *ahci_dev, uint8_t port)
{
    struct ahci_ioport *pp = &ahci_dev->port[port];
    uint64_t port_mmio = pp->port_mmio;
    uint32_t port_status;

    port_status = ahci_readl(port_mmio + PORT_SCR_STAT);
    if ((port_status & 0xf) != 0x03)
    {
        ahci_printf("no link on port %u\n", port);
        return -1;
    }

    // align to 1024 bytes and set 0
    // 32-lot cmd 32 * 32
    // 256
    // 128
    // 56 * 16
    uint64_t mem = (uint64_t)ahci_malloc_align(AHCI_PORT_PRIV_DMA_SZ, 1024);
    ahci_memset((void *)mem, 0, AHCI_PORT_PRIV_DMA_SZ);

    // First item in chunk of DMA memory
    // 32-slot command table, 32 bytes each in size
    pp->cmd_slot = (struct ahci_cmd_hdr *)mem;
    pp->cmd_slot_dma = ahci_virt_to_phys(mem);
    //ahci_printf("cmd_slot = 0x%016lx, cmd_slot_dma = 0x%016lx\n",
    //        pp->cmd_slot, pp->cmd_slot_dma);

    mem += AHCI_CMD_SLOT_SZ;

    // Second item
    // Received-FIS area, 256 bytes aligned
    pp->rx_fis = mem;
    pp->rx_fis_dma = ahci_virt_to_phys(mem);
    //ahci_printf("rx_fis = 0x%016lx, rx_fis_dma = 0x%016lx\n",
    //        pp->rx_fis, pp->rx_fis_dma);

    mem += AHCI_RX_FIS_SZ;

    // Third item
    // data area for storing a single command 128 bytes
    // and its scatter-gather table 56 * 16 bytes
    pp->cmd_tbl = mem;
    pp->cmd_tbl_dma = ahci_virt_to_phys(mem);
    //ahci_printf("cmd_tbl = 0x%016lx, cmd_tbl_dma = 0x%016lx\n",
    //        pp->cmd_tbl, pp->cmd_tbl_dma);
    mem += AHCI_CMD_TBL_HDR_SZ;
    pp->cmd_tbl_sg = (struct ahci_sg *)mem;
    //ahci_printf("cmd_tbl_sg = 0x%016lx,\n", pp->cmd_tbl_sg);

    ahci_writel((pp->cmd_slot_dma & 0xffffffff), port_mmio + PORT_LST_ADDR);
    ahci_writel((pp->cmd_slot_dma >> 32), port_mmio + PORT_LST_ADDR_HI);
    ahci_writel((pp->rx_fis_dma & 0xffffffff), port_mmio + PORT_FIS_ADDR);
    ahci_writel((pp->rx_fis_dma >> 32), port_mmio + PORT_FIS_ADDR_HI);

    // start port
    ahci_writel(PORT_CMD_ICC_ACTIVE | PORT_CMD_FIS_RX |
            PORT_CMD_POWER_ON | PORT_CMD_SPIN_UP | PORT_CMD_START,
            port_mmio + PORT_CMD);

    // wait port to be ready, ~3ms
    uint32_t timeout = 200, tmp = 0;
    do
    {
        ahci_mdelay(1);
        tmp = ahci_readl(port_mmio + PORT_TFDATA);
        tmp &= (ATA_ERR | ATA_DRQ | ATA_BUSY);
    }
    while (tmp && --timeout);

    if (timeout <= 0)
    {
        ahci_printf("ahci port %u failed to start\n", port);
        return -1;
    }

    return 0;
}

// get ata id
void ahci_sata_identify(struct ahci_device *ahci_dev, uint16_t *id)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D; // 0
    cfis.pm_port_c = 0x80; // 1
    cfis.command = ATA_CMD_ID_ATA; // 2

    ahci_exec_ata_cmd(ahci_dev, port, &cfis, id, ATA_ID_WORDS * 2,
                    READ_CMD);
}

// set cmd for lba28
uint32_t ahci_sata_rw_cmd(struct ahci_device *ahci_dev, uint32_t start,
                        uint32_t blkcnt, void *buffer, uint32_t is_write)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;
    uint32_t block = start;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D; // 0
    cfis.pm_port_c = 0x80; // 1
    cfis.command = (is_write) ? ATA_CMD_WRITE : ATA_CMD_READ; // 2
    cfis.lba_low = block & 0xff; // 4
    cfis.lba_mid = (block >> 8) & 0xff; // 5
    cfis.lba_high = (block >> 16) & 0xff; // 6
    cfis.device = ATA_LBA; // 7
    cfis.device |= (block >> 24) & 0xf;
    cfis.sector_count = blkcnt & 0xff; // 12

    if (ahci_exec_ata_cmd(ahci_dev, port, &cfis, buffer,
                          ATA_SECT_SIZE * blkcnt, is_write) > 0)
        return blkcnt;
    else
        return 0;
}

// flush cache for lba28
void ahci_sata_flush_cache(struct ahci_device *ahci_dev)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D; // 0
    cfis.pm_port_c = 0x80; // 1
    cfis.command = ATA_CMD_FLUSH; // 2

    ahci_exec_ata_cmd(ahci_dev, port, &cfis, NULL, 0, READ_CMD);
}

// read/write for lba28
uint32_t ata_low_level_rw_lba28(struct ahci_device *ahci_dev, uint64_t blknr,
                            uint32_t blkcnt, void *buffer, uint32_t is_write)
{
    uint32_t start = blknr;
    uint32_t blks = blkcnt;
    uint32_t max_blks = ATA_MAX_SECTORS;
    void *addr = buffer;

    max_blks = ATA_MAX_SECTORS;
    do
    {
        if (blks > max_blks)
        {
            if (max_blks != ahci_sata_rw_cmd(ahci_dev, start, max_blks, addr, is_write))
                return 0;
            start += max_blks;
            blks -= max_blks;
            addr += ATA_SECT_SIZE * max_blks;
        }
        else
        {
            if (blks != ahci_sata_rw_cmd(ahci_dev, start, blks, addr, is_write))
                return 0;
            start += blks;
            blks = 0;
            addr += ATA_SECT_SIZE * blks;
        }
    } while (blks != 0);

    return blkcnt;
}

uint32_t ahci_sata_rw_cmd_ext(struct ahci_device *ahci_dev, uint64_t start,
                            uint32_t blkcnt, void *buffer, uint32_t is_write)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;
    uint64_t block;

    block = start;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D; // 0
    cfis.pm_port_c = 0x80; // 1
    cfis.command = (is_write) ? ATA_CMD_WRITE_EXT : ATA_CMD_READ_EXT; // 2
    cfis.lba_low = block & 0xff; // 4
    cfis.lba_mid = (block >> 8) & 0xff; // 5
    cfis.lba_high = (block >> 16) & 0xff; // 6
    cfis.device = ATA_LBA; // 7
    cfis.lba_low_exp = (block >> 24) & 0xff; // 8
    cfis.lba_mid_exp = (block >> 32) & 0xff; // 9
    cfis.lba_high_exp = (block >> 40) & 0xff; // 10
    cfis.sector_count = blkcnt & 0xff; // 12
    cfis.sector_count_exp = (blkcnt >> 8) & 0xff; // 13

    // 512 bytes * blkcnt
    if (ahci_exec_ata_cmd(ahci_dev, port, &cfis, buffer,
                          ATA_SECT_SIZE * blkcnt, is_write) > 0)
        return blkcnt;
    else
        return 0;
}

// flush cache for lba48
void ahci_sata_flush_cache_ext(struct ahci_device *ahci_dev)
{
    struct sata_fis_h2d cfis = {0};
    uint8_t port = ahci_dev->port_idx;

    cfis.fis_type = SATA_FIS_TYPE_REGISTER_H2D; // 0
    cfis.pm_port_c = 0x80; // 1
    cfis.command = ATA_CMD_FLUSH_EXT; // 2

    ahci_exec_ata_cmd(ahci_dev, port, &cfis, NULL, 0, READ_CMD);
}

// read/write for lba48
uint32_t ata_low_level_rw_lba48(struct ahci_device *ahci_dev, uint64_t blknr,
                                uint32_t blkcnt, void *buffer, uint32_t is_write)
{
    uint64_t start = blknr;
    uint32_t blks = blkcnt;
    uint32_t max_blks = ATA_MAX_SECTORS_LBA48;
    void *addr = buffer;

    do
    {
        if (blks > max_blks)
        {
            if (max_blks != ahci_sata_rw_cmd_ext(ahci_dev, start, max_blks, addr, is_write))
                return 0;
            start += max_blks;
            blks -= max_blks;
            addr += ATA_SECT_SIZE * max_blks;
        }
        else
        {
            if (blks != ahci_sata_rw_cmd_ext(ahci_dev, start, blks, addr, is_write))
                return 0;
            start += blks;
            blks = 0;
            addr += ATA_SECT_SIZE * blks;
        }
    } while (blks != 0);

    return blkcnt;
}

int ahci_port_scan(struct ahci_device *ahci_dev)
{
    uint32_t linkmap = ahci_dev->port_map_linkup;

    if (linkmap == 0)
    {
        ahci_printf("no port device detected\n");
        return -1;
    }

    // get the lsb available port
    for (uint8_t i = 0; i < ahci_dev->n_ports; ++ i)
    {
        if ((linkmap >> i) & 0x01)
        {
            if (ahci_port_start(ahci_dev, i))
            {
                ahci_printf("cannot start port %u\n", i);
                return -1;
            }

            ahci_dev->port_idx = i;
            break;
        }
    }

    return 0;
}

void ahci_sata_xfer_mode(struct ahci_device *ahci_dev, uint16_t *id)
{
    // get pio and udma
    ahci_dev->pio_mask = id[ATA_ID_PIO_MODES];
    ahci_dev->udma_mask = id[ATA_ID_UDMA_MODES];
}

void ahci_sata_init_wcache(struct ahci_device *ahci_dev, uint16_t *id)
{
    if (ata_id_has_wcache(id) && ata_id_wcache_enabled(id))
        ahci_dev->flags |= SATA_FLAG_WCACHE;
    if (ata_id_has_flush(id))
        ahci_dev->flags |= SATA_FLAG_FLUSH;
    if (ata_id_has_flush_ext(id))
        ahci_dev->flags |= SATA_FLAG_FLUSH_EXT;
}

void ahci_sata_scan(struct ahci_device *ahci_dev)
{
    struct ahci_blk_dev *pdev = &ahci_dev->blk_dev;

    uint16_t id[ATA_ID_WORDS + 1];
    uint8_t serial[ATA_ID_SERNO_LEN + 1];
    uint8_t revision[ATA_ID_FW_REV_LEN + 1];
    uint8_t product[ATA_ID_PROD_LEN + 1];

    // identify device
    // read info into id
    ahci_sata_identify(ahci_dev, id);

    // product model
    ata_id_c_string(id, product, ATA_ID_PROD, sizeof(product));
    ahci_memcpy(pdev->product, product, sizeof(product));

    // serial number
    ata_id_c_string(id, serial, ATA_ID_SERNO, sizeof(serial));
    ahci_memcpy(pdev->serial, serial, sizeof(serial));

    // firmware version
    ata_id_c_string(id, revision, ATA_ID_FW_REV, sizeof(revision));
    ahci_memcpy(pdev->revision, revision, sizeof(revision));

    // get sector nums
    pdev->lba = ata_id_n_sectors(id);
    // set vector size fixed 512
    pdev->blksz = ATA_SECT_SIZE;
    pdev->lba48 = ata_id_has_lba48(id);
    // get ncq depth
    pdev->queue_depth = ata_id_queue_depth(id);

    // get the xfer mode from device
    ahci_sata_xfer_mode(ahci_dev, id);

    // get the write cache status from device
    ahci_sata_init_wcache(ahci_dev, id);

    // set the udma to highest speed
    uint8_t subcmd = SETFEATURES_XFER;
    uint8_t action = (ahci_ffs32(ahci_dev->udma_mask + 1) + 0x3e);
    ahci_set_feature(ahci_dev, subcmd, action);

    // print sata info
    ahci_sata_print_info(pdev);

    // try to read the first sector
    // ahci_sata_read_common(ahci_dev, 0, 1, sector_data);
    // dump_buffer(sector_data, 512);
}

// 读函数
uint32_t ahci_sata_read_common(struct ahci_device *ahci_dev, uint64_t blknr,
                               uint32_t blkcnt, void *buffer)
{
    struct ahci_blk_dev *pdev = &ahci_dev->blk_dev;

    uint32_t rc;
    if (pdev->lba48)
        rc = ata_low_level_rw_lba48(ahci_dev, blknr, blkcnt, buffer, READ_CMD);
    else
        rc = ata_low_level_rw_lba28(ahci_dev, blknr, blkcnt, buffer, READ_CMD);

    return rc;
}

// 写函数
uint32_t ahci_sata_write_common(struct ahci_device *ahci_dev, uint64_t blknr,
                                uint32_t blkcnt, void *buffer)
{
    struct ahci_blk_dev *pdev = &ahci_dev->blk_dev;
    uint32_t flags = ahci_dev->flags;

    uint32_t rc;
    if (pdev->lba48)
    {
        rc = ata_low_level_rw_lba48(ahci_dev, blknr, blkcnt, buffer, WRITE_CMD);
        if ((flags & SATA_FLAG_WCACHE) && (flags & SATA_FLAG_FLUSH_EXT))
            ahci_sata_flush_cache_ext(ahci_dev);
    }
    else
    {
        rc = ata_low_level_rw_lba28(ahci_dev, blknr, blkcnt, buffer, WRITE_CMD);
        if ((flags & SATA_FLAG_WCACHE) && (flags & SATA_FLAG_FLUSH))
            ahci_sata_flush_cache(ahci_dev);
    }

    return rc;
}

int ahci_init(struct ahci_device *ahci_dev)
{
    // set ahci base
    ahci_dev->mmio_base = ahci_phys_to_uncached(0x400e0000);

    // init ahci host and port
    int ret = ahci_host_init(ahci_dev);
    if (ret)
        return -1;

    // scan ahci port
    ret = ahci_port_scan(ahci_dev);
    if (ret)
        return -1;

    // print ahci info
    ahci_print_info(ahci_dev);

    // scan sata
    ahci_sata_scan(ahci_dev);

    return 0;
}
