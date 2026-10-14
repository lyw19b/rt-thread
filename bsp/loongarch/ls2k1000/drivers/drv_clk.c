#include <rtthread.h>
#include <rthw.h>

#include <loongarch.h>
#include <ls2k1000la.h>
#include <drv_clk.h>
#include <io.h>

// node pll
// refclk -> DIVIDER -> PLL -> NODE DIVIDER -> node_clock (~1GHz) -> FREQ_SCALE

// pix pll (2)
// refclk -> DIVIDER -> PLL -> PIX DIVIDER -> pix_clock (100-250MHz)

// ddr pll
// refclk -> DIVIDER -> PLL -> DDR DIVIDER -> ddr_clock (400-700MHz)
//                             GPU DIVIDER -> gpu_clock (300-500MHz)
//                             HDA DIVIDER -> hda_clock (fixed 24MHz)

// dc pll
// refclk -> DIVIDER -> PLL -> DC DIVIDER -> dc_clock (~200MHz)
//                                        -> GMAC DIVIER -> gmac_clock (125MHz)
//                                                       -> FREQ_SCALE -> abp_clock
//                                                       -> FREQ_SCALE -> sata_clock
//                                                       -> FREQ_SCALE -> usb_clock

// refclk 100MHz
#define REF_FREQ 100

// working improperly in qemu-2k1000

rt_uint64_t clk_get_cpu_rate()
{
    rt_uint64_t node_clk, cpu_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;
    rt_uint64_t freqscale;

    node_clk = REF_FREQ * 1000;

    ctrl = readq(LS_NODE_PLL_L);
    l1_div_ref = (ctrl >> NODE_L1DIV_REF_SHIFT) & NODE_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> NODE_L1DIV_LOOPC_SHIFT) & NODE_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_NODE_PLL_H);
    l2_div_out = (ctrl >> NODE_L2DIV_OUT_SHIFT) & NODE_L2DIV_OUT_MARK;
    div = l1_div_ref * l2_div_out;

    // node clk
    node_clk = (node_clk * mult / div) * 1000;

    ctrl = readq(LS_FREQ_SCALE);
    freqscale = (ctrl >> FREQSCALE_NODE_SHIFT) & FREQSCALE_NODE_MARK;

    // cpu clk
    cpu_clk = node_clk * (freqscale + 1) / 8;
    return cpu_clk;
}

rt_uint64_t clk_get_pix0_rate()
{
    rt_uint64_t pix0_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    pix0_clk = REF_FREQ * 1000;

    ctrl = readq(LS_PIX0_PLL_L);
    l1_div_ref = (ctrl >> PIX_L1DIV_REF_SHIFT) & PIX_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> PIX_L1DIV_LOOPC_SHIFT) & PIX_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_PIX0_PLL_H);
    l2_div_out = (ctrl >> PIX_L2DIV_OUT_PIX_SHIFT) & PIX_L2DIV_OUT_PIX_MARK;
    div = l1_div_ref * l2_div_out;

    // pix0 clk
    pix0_clk = (pix0_clk * mult / div) * 1000;
    return pix0_clk;
}

rt_uint64_t clk_get_pix1_rate()
{
    rt_uint64_t pix1_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    pix1_clk = REF_FREQ * 1000;

    ctrl = readq(LS_PIX1_PLL_L);
    l1_div_ref = (ctrl >> PIX_L1DIV_REF_SHIFT) & PIX_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> PIX_L1DIV_LOOPC_SHIFT) & PIX_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_PIX1_PLL_H);
    l2_div_out = (ctrl >> PIX_L2DIV_OUT_PIX_SHIFT) & PIX_L2DIV_OUT_PIX_MARK;
    div = l1_div_ref * l2_div_out;

    // pix1 clk
    pix1_clk = (pix1_clk * mult / div) * 1000;
    return pix1_clk;
}

rt_uint64_t clk_get_ddr_rate()
{
    rt_uint64_t ddr_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    ddr_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DDR_PLL_L);
    l1_div_ref = (ctrl >> DDR_L1DIV_REF_SHIFT) & DDR_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DDR_L1DIV_LOOPC_SHIFT) & DDR_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DDR_PLL_H);
    l2_div_out = (ctrl >> DDR_L2DIV_OUT_DDR_SHIFT) & DDR_L2DIV_OUT_DDR_MARK;
    div = l1_div_ref * l2_div_out;

    // ddr clk
    ddr_clk = (ddr_clk * mult / div) * 1000;
    return ddr_clk;
}

rt_uint64_t clk_get_gpu_rate()
{
    rt_uint64_t gpu_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    gpu_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DDR_PLL_L);
    l1_div_ref = (ctrl >> DDR_L1DIV_REF_SHIFT) & DDR_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DDR_L1DIV_LOOPC_SHIFT) & DDR_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DDR_PLL_H);
    l2_div_out = (ctrl >> DDR_L2DIV_OUT_GPU_SHIFT) & DDR_L2DIV_OUT_GPU_MARK;
    div = l1_div_ref * l2_div_out;

    // gpu clk
    gpu_clk = (gpu_clk * mult / div) * 1000;
    return gpu_clk;
}

rt_uint64_t clk_get_hda_rate()
{
    rt_uint64_t hda_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    hda_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DDR_PLL_L);
    l1_div_ref = (ctrl >> DDR_L1DIV_REF_SHIFT) & DDR_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DDR_L1DIV_LOOPC_SHIFT) & DDR_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DDR_PLL_H);
    l2_div_out = (ctrl >> DDR_L2DIV_OUT_HDA_SHIFT) & DDR_L2DIV_OUT_HDA_MARK;
    div = l1_div_ref * l2_div_out;

    // hda clk
    hda_clk = (hda_clk * mult / div) * 1000;
    return hda_clk;
}

rt_uint64_t clk_get_dc_rate()
{
    rt_uint64_t dc_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    dc_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DC_PLL_L);
    l1_div_ref = (ctrl >> DC_L1DIV_REF_SHIFT) & DC_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DC_L1DIV_LOOPC_SHIFT) & DC_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DC_PLL_H);
    l2_div_out = (ctrl >> DC_L2DIV_OUT_DC_SHIFT) & DC_L2DIV_OUT_DC_MARK;
    div = l1_div_ref * l2_div_out;

    // dc clk
    dc_clk = (dc_clk * mult / div) * 1000;
    return dc_clk;
}

rt_uint64_t clk_get_gmac_rate()
{
    rt_uint64_t gmac_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;

    gmac_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DC_PLL_L);
    l1_div_ref = (ctrl >> DC_L1DIV_REF_SHIFT) & DC_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DC_L1DIV_LOOPC_SHIFT) & DC_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DC_PLL_H);
    l2_div_out = (ctrl >> DC_L2DIV_OUT_GMAC_SHIFT) & DC_L2DIV_OUT_GMAC_MARK;
    div = l1_div_ref * l2_div_out;

    // gmac clk
    gmac_clk = (gmac_clk * mult / div) * 1000;
    return gmac_clk;
}

rt_uint64_t clk_get_apb_rate()
{
    rt_uint64_t apb_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;
    rt_uint64_t freqscale;

    apb_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DC_PLL_L);
    l1_div_ref = (ctrl >> DC_L1DIV_REF_SHIFT) & DC_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DC_L1DIV_LOOPC_SHIFT) & DC_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DC_PLL_H);
    l2_div_out = (ctrl >> DC_L2DIV_OUT_DC_SHIFT) & DC_L2DIV_OUT_DC_MARK;
    div = l1_div_ref * l2_div_out;

    // gmac clk
    apb_clk = (apb_clk * mult / div) * 1000;

    ctrl = readq(LS_FREQ_SCALE);
    freqscale = (ctrl >> FREQSCALE_APB_SHIFT) & FREQSCALE_APB_MARK;

    // apb clk
    apb_clk = apb_clk * (freqscale + 1) / 8;
    return apb_clk;
}

rt_uint64_t clk_get_sata_rate()
{
    rt_uint64_t sata_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;
    rt_uint64_t freqscale;

    sata_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DC_PLL_L);
    l1_div_ref = (ctrl >> DC_L1DIV_REF_SHIFT) & DC_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DC_L1DIV_LOOPC_SHIFT) & DC_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DC_PLL_H);
    l2_div_out = (ctrl >> DC_L2DIV_OUT_DC_SHIFT) & DC_L2DIV_OUT_DC_MARK;
    div = l1_div_ref * l2_div_out;

    // gmac clk
    sata_clk = (sata_clk * mult / div) * 1000;

    ctrl = readq(LS_FREQ_SCALE);
    freqscale = (ctrl >> FREQSCALE_SATA_SHIFT) & FREQSCALE_SATA_MARK;

    // sata clk
    sata_clk = sata_clk * (freqscale + 1) / 8;
    return sata_clk;
}

rt_uint64_t clk_get_usb_rate()
{
    rt_uint64_t usb_clk;
    rt_uint64_t ctrl;

    rt_uint64_t l1_div_ref;
    rt_uint64_t l1_div_loopc;
    rt_uint64_t l2_div_out;
    rt_uint64_t mult, div;
    rt_uint64_t freqscale;

    usb_clk = REF_FREQ * 1000;

    ctrl = readq(LS_DC_PLL_L);
    l1_div_ref = (ctrl >> DC_L1DIV_REF_SHIFT) & DC_L1DIV_REF_MARK;
    l1_div_loopc = (ctrl >> DC_L1DIV_LOOPC_SHIFT) & DC_L1DIV_LOOPC_MARK;
    mult = l1_div_loopc;

    ctrl = readq(LS_DC_PLL_H);
    l2_div_out = (ctrl >> DC_L2DIV_OUT_DC_SHIFT) & DC_L2DIV_OUT_DC_MARK;
    div = l1_div_ref * l2_div_out;

    // gmac clk
    usb_clk = (usb_clk * mult / div) * 1000;

    ctrl = readq(LS_FREQ_SCALE);
    freqscale = (ctrl >> FREQSCALE_USB_SHIFT) & FREQSCALE_USB_MARK;

    // usb clk
    usb_clk = usb_clk * (freqscale + 1) / 8;
    return usb_clk;
}
