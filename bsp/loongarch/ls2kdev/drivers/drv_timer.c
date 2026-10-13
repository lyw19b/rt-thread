#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include <loongarch.h>
#include <ls2k1000la.h>
#include <drv_timer.h>


rt_uint64_t read_const_freq(void)
{
    rt_uint64_t res;
    rt_uint64_t base_freq;
    rt_uint64_t cfm, cfd;

    // read CPUCFG2
    res = read_cpucfg(LOONGARCH_CPUCFG2);
    // constant timer LLFTP
    if (!(res & CPUCFG2_LLFTP))
        return 0;

    // read frequency of oscillator
    base_freq = read_cpucfg(LOONGARCH_CPUCFG4);
    // low ccmul，high ccdiv
    res = read_cpucfg(LOONGARCH_CPUCFG5);
    cfm = res & 0xffff;
    cfd = (res >> 16) & 0xffff;

    if (!base_freq || !cfm || !cfd)
        return 0;

    return (base_freq * cfm / cfd);
}
