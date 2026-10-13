#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include <loongarch.h>
#include <ls2k1000la.h>
#include <board.h>
#include <io.h>
#include <drv_pci.h>
#include <drv_uart.h>
#include <drv_interrupt.h>
#include <timer.h>
#include <drv_timer.h>
#include <drv_power.h>

void rt_hw_board_info_dump()
{
#define PRID_COMP_MASK      0xff0000
#define PRID_SERIES_MASK    0x00f000
#define PRID_PRODUCT_MASK   0x000fff

    rt_uint32_t prid = read_cpucfg(LOONGARCH_CPUCFG0);
    rt_kprintf("prid: %u\n", prid);

    rt_uint32_t cpuid = csr_read32(LOONGARCH_IOCSR_CPUNAME);
    rt_kprintf("cpuid: %u\n", cpuid & PRID_PRODUCT_MASK);

    rt_kprintf("const_clock_freq: %lu\n", read_const_freq());
}

void rt_hw_board_init(void)
{
    // init heap memory system
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);

    // init hardware interrupt
    rt_hw_interrupt_init();

    // init hardware uart device
    rt_hw_uart_init();
    // set console device
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);

    // init operating system timer
    rt_hw_timer_init();
    // set a tick hook
    rt_tick_sethook(RT_NULL);

    // set a idle hook
    rt_thread_idle_sethook(rt_hw_cpu_idle);
    // rt_thread_idle_sethook(RT_NULL);

    // init components
    rt_components_board_init();

    rt_kprintf("heap: [%p - %p]\n", (void *)RT_HW_HEAP_BEGIN, (void *)RT_HW_HEAP_END);

    // dump board info
    rt_hw_board_info_dump();
}
