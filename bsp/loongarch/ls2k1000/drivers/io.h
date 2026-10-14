#ifndef __LS2K_IO_H__
#define __LS2K_IO_H__

#include <rtthread.h>

#define writeb(v,a)   (*(volatile rt_uint8_t  *)(a) = (v))
#define writew(v,a)   (*(volatile rt_uint16_t *)(a) = (v))
#define writel(v,a)   (*(volatile rt_uint32_t *)(a) = (v))
#define writeq(v,a)   (*(volatile rt_uint64_t *)(a) = (v))

#define readb(a)      (*(volatile rt_uint8_t  *)(a))
#define readw(a)      (*(volatile rt_uint16_t *)(a))
#define readl(a)      (*(volatile rt_uint32_t *)(a))
#define readq(a)      (*(volatile rt_uint64_t *)(a))

#endif // __LS2K_IO_H__
