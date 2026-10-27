/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: main lwip demo.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "loongson_board_info.h"
#include "loongson_soc_info.h"
#include "drv_power.h"
#include "drv_interrupt.h"
#include "drv_gpio.h"
#include "drv_spi_io.h"
#include "drv_pwm.h"
#include "drv_rtc.h"
#include "drv_watchdog.h"

#include "bsp_pinmux.h"

extern unsigned int __bss_start;
extern unsigned int __bss_end;

#define RT_HW_HEAP_SIZE  200 * 1024 * 1024
#define RT_HW_HEAP_BEGIN ((void *)&__bss_end)
#define RT_HW_HEAP_END   ((void *)(RT_HW_HEAP_BEGIN + RT_HW_HEAP_SIZE))

#include <lwip/tcpip.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>

void e0_set_static_ip(void)
{
    struct netif *netif;

    // 等待网卡初始化完成
    rt_thread_mdelay(100);

    netif = netif_find("e0");
    if (netif == RT_NULL)
    {
        rt_kprintf("e0 not ready\n");
        return;
    }

    // 停止 DHCP 如果正在运行
    // dhcp_stop(netif);

    // 设置静态地址
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 1, 159);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);

    netif_set_addr(netif, &ipaddr, &netmask, &gw);

    rt_kprintf("e0 IP set to 192.168.1.159\n");
}

// INIT_APP_EXPORT(e0_set_static_ip);

void rt_hw_board_init(void)
{
    int i;
    unsigned long long addr;
    rt_system_heap_init(RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);

    // init hardware interrupt
    rt_hw_interrupt_init();

    // addr = 0x8000000016000100;
    // writel(0x1ee20000, addr);
    addr = 0x800000001600010C;
    writel(0x3e3e, addr);
    addr = 0x8000000016000110;
    writel(0x1f57cc00, addr);

    // init hardware uart device
    rt_hw_uart_init();
    // set console device
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);

    rt_hw_timer_init(120000000);
    // set a tick hook
    rt_tick_sethook(RT_NULL);
    // set a idle hook
    rt_thread_idle_sethook(rt_hw_cpu_idle);

    // init components
    rt_components_board_init();

    rt_hw_gpio_init();
    rt_hw_spi_flash_init();
    rt_hw_spi_io_init();
    rt_hw_i2c_init();
    rt_hw_pwm_init();
    rt_hw_rtc_init();
    rt_hw_ls2x_wdt_init();
    rt_kprintf("%llx %llx\n", RT_HW_HEAP_BEGIN, RT_HW_HEAP_END);

    for (i = 28; i < 40; ++i)
    {
        pinmux_pin_setup(i, MODE_AS_MAIN_FUNC);
    }
    return;
}

int main(int argc, char **argv)
{
    rt_kprintf("Hi, this is RT-Thread!!\n");
    rt_kprintf("\033[1m\033[32mHello LoongArch64!\033[0m\n");

    rt_pin_write(75, 1);

    rt_thread_mdelay(2000);
    e0_set_static_ip();

    while (1)
    {
        rt_thread_mdelay(1000);
    }

    return 0;
}
