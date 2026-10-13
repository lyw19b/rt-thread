#ifndef __LS2K_CLOCK_H__
#define __LS2K_CLOCK_H__

#include <rtthread.h>

rt_uint64_t clk_get_pll_rate(void);
rt_uint64_t clk_get_cpu_rate(void);
rt_uint64_t clk_get_ddr_rate(void);
rt_uint64_t clk_get_apb_rate(void);
rt_uint64_t clk_get_dc_rate(void);

#endif // __LS2K_CLOCK_H__
