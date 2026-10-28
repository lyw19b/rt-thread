/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add Explorer read-only board checks
 */

/*
 * Description:
 *   Check Explorer peripherals without changing persistent data or output pins.
 * Notes:
 *   SPI tests send only JEDEC ID reads to CS0. No I2C address is assumed to
 *   contain an EEPROM: the modeled SPD test is a separate acceptance command.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <sys/time.h>
#include "hpet_counter.h"

static int read_flash_id(struct rt_spi_device *device, rt_uint8_t id[3])
{
    rt_uint8_t opcode = 0x9f;

    return rt_spi_send_then_recv(device, &opcode, 1, id, 3);
}

static int check_flash(unsigned int bus)
{
    struct rt_spi_configuration config = {
        .mode = RT_SPI_MSB | RT_SPI_MODE_0,
        .data_width = 8,
        .max_hz = 1000000,
    };
    char name[12];
    rt_uint8_t expected[3];
    struct rt_spi_device *device;

    rt_snprintf(name, sizeof(name), "spi%ucs0", bus);
    device = (struct rt_spi_device *)rt_device_find(name);

    if (!device || rt_spi_configure(device, &config) || read_flash_id(device, expected) ||
        !expected[0] || expected[0] == 0xff)
    {
        rt_kprintf("BOARD SPI%u UNAVAILABLE\n", bus);
        return 0;
    }

    for (unsigned int i = 0; i < 256; ++i)
    {
        rt_uint8_t actual[3];

        if (read_flash_id(device, actual) || rt_memcmp(actual, expected, 3))
        {
            rt_kprintf("BOARD SPI%u FAIL round=%u\n", bus, i);
            return -1;
        }

        rt_thread_mdelay(1);
    }

    rt_kprintf("BOARD SPI%u PASS JEDEC=%02x%02x%02x reads=257\n",
               bus, expected[0], expected[1], expected[2]);
    return 1;
}

static int check_rtc(void)
{
    rt_device_t rtc = rt_device_find("rtc");
    struct timeval before;
    struct timeval after;

    if (!rtc || rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &before))
    {
        rt_kprintf("BOARD RTC UNAVAILABLE\n");
        return 0;
    }

    rt_thread_mdelay(1200);

    if (rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &after) ||
        after.tv_sec < before.tv_sec || after.tv_sec - before.tv_sec > 3 ||
        (after.tv_sec == before.tv_sec && after.tv_usec <= before.tv_usec))
    {
        rt_kprintf("BOARD RTC FAIL: clock did not advance\n");
        return -1;
    }

    rt_kprintf("BOARD RTC PASS epoch=%ld usec=%ld\n", (long)after.tv_sec, (long)after.tv_usec);
    return 1;
}

static int check_hpet(void)
{
    rt_device_t hpet = rt_device_find("hpet0");
    rt_uint32_t period;
    rt_uint64_t before;
    rt_uint64_t after;
    int result;

    if (!hpet || rt_device_open(hpet, RT_DEVICE_OFLAG_RDONLY))
    {
        rt_kprintf("BOARD HPET UNAVAILABLE\n");
        return 0;
    }

    result = rt_device_control(hpet, LS2K3000_HPET_GET_PERIOD, &period);

    if (!result && rt_device_read(hpet, 0, &before, sizeof(before)) == sizeof(before))
    {
        rt_thread_mdelay(100);
        result = rt_device_read(hpet, 0, &after, sizeof(after)) == sizeof(after) &&
                 after > before ? 1 : -1;

        if (result > 0)
        {
            rt_kprintf("BOARD HPET PASS period_fs=%u delta=%llu\n",
                       period, (unsigned long long)(after - before));
        }
        else
        {
            rt_kprintf("BOARD HPET FAIL: counter did not advance\n");
        }
    }
    else
    {
        result = 0;
        rt_kprintf("BOARD HPET UNAVAILABLE: counter disabled or unreadable\n");
    }

    rt_device_close(hpet);
    return result;
}

static long ls2k3000_board_test(void)
{
    static const char *names[] = {"rtc", "hpet0", "pin", "pwm0", "wdt0", "i2c0", "i2c1"};
    unsigned int passed = 0;
    unsigned int unavailable = 0;
    unsigned int failed = 0;
    int results[4];

    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        rt_kprintf("BOARD device=%s registered=%u\n", names[i], !!rt_device_find(names[i]));
    }

    results[0] = check_rtc();
    results[1] = check_hpet();
    results[2] = check_flash(0);
    results[3] = check_flash(1);

    for (unsigned int i = 0; i < sizeof(results) / sizeof(results[0]); ++i)
    {
        passed += results[i] > 0;
        unavailable += results[i] == 0;
        failed += results[i] < 0;
    }

    rt_kprintf("BOARD READ COMPLETE passed=%u unavailable=%u failed=%u\n",
               passed, unavailable, failed);
    return failed ? -RT_ERROR : RT_EOK;
}
MSH_CMD_EXPORT(ls2k3000_board_test, Read Explorer RTC HPET and SPI without changing persistent data);
