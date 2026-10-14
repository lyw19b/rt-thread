#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>

#include <ls2k1000la.h>
#include <drv_power.h>
#include <io.h>

extern void cpu_idle();
rt_uint64_t idle_count = 0;

void rt_hw_cpu_idle()
{
    idle_count ++;
    cpu_idle();
}

void cpu_idle_count()
{
    rt_kprintf("idle_count = %u\n", idle_count);
}
MSH_CMD_EXPORT_ALIAS(cpu_idle_count, idle_count, show cpu idle count);

void rt_hw_cpu_reset()
{
    rt_kprintf("reboot system...\n");
    writel(0x1, LS_RST_CNT_REG);
    while (1);
}
MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_reset, reboot, reboot cpu);

void rt_hw_cpu_shutdown()
{
    rt_kprintf("shutdown system...\n");
    writel(0xffffffff, LS_PM1_STS_REG);
    writel(0x1c00, LS_PM1_CNT_REG);
    writel(0x3c00, LS_PM1_CNT_REG);
    while (1);
}
MSH_CMD_EXPORT_ALIAS(rt_hw_cpu_shutdown, shutdown, shutdown cpu);
