/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Run explicit read-only peripheral checks through the RT-Thread device APIs.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <sys/time.h>

static long ls2k3000_periph_test(void)
{
    unsigned int passed = 0;
    unsigned int unavailable = 0;
    rt_device_t rtc = rt_device_find("rtc");
    struct timeval before;
    struct timeval after;

    if (rtc && !rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &before))
    {
        rt_thread_mdelay(1200);

        if (!rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &after) &&
            after.tv_sec >= before.tv_sec && after.tv_sec - before.tv_sec <= 3 &&
            (after.tv_sec > before.tv_sec || after.tv_usec > before.tv_usec))
        {
            ++passed;
            rt_kprintf("PERIPH RTC PASS epoch=%ld\n", (long)after.tv_sec);
        }
        else
        {
            rt_kprintf("PERIPH RTC FAIL: counter not advancing\n");
            return -RT_ERROR;
        }
    }
    else
    {
        ++unavailable;
        rt_kprintf("PERIPH RTC UNAVAILABLE\n");
    }

    for (unsigned int i = 0; i < 2; ++i)
    {
        char name[12];
        uint8_t opcode = 0x9f;
        uint8_t id[3] = {0};
        struct rt_spi_configuration config = {
            .mode = RT_SPI_MSB | RT_SPI_MODE_0,
            .data_width = 8,
            .max_hz = 1000000,
        };

        rt_snprintf(name, sizeof(name), "spi%ucs0", i);
        struct rt_spi_device *device = (struct rt_spi_device *)rt_device_find(name);

        if (device && !rt_spi_configure(device, &config) &&
            !rt_spi_send_then_recv(device, &opcode, 1, id, sizeof(id)) &&
            id[0] != 0 && id[0] != 0xff)
        {
            ++passed;
            rt_kprintf("PERIPH SPI%u PASS JEDEC=%02x%02x%02x\n", i, id[0], id[1], id[2]);
        }
        else
        {
            ++unavailable;
            rt_kprintf("PERIPH SPI%u UNAVAILABLE\n", i);
        }
    }

    struct rt_i2c_bus_device *i2c = rt_i2c_bus_device_find("i2c0");
    uint8_t offset = 0;
    uint8_t spd[16];
    struct rt_i2c_msg messages[2] = {
        {.addr = 0x50, .len = 1, .buf = &offset},
        {.addr = 0x50, .flags = RT_I2C_RD, .len = sizeof(spd), .buf = spd},
    };

    /* Only an EEPROM address-pointer write precedes the read; no data write. */
    if (i2c && rt_i2c_transfer(i2c, messages, 2) == 2)
    {
        ++passed;
        rt_kprintf("PERIPH I2C0 PASS SPD=%02x%02x%02x%02x\n", spd[0], spd[1], spd[2], spd[3]);
    }
    else
    {
        ++unavailable;
        rt_kprintf("PERIPH I2C0 UNAVAILABLE\n");
    }

    if (i2c)
    {
        messages[0].addr = 0x80;

        if (rt_i2c_transfer(i2c, messages, 1) >= 0)
        {
            return -RT_ERROR;
        }

        ++passed;
        rt_kprintf("PERIPH invalid address PASS\n");
    }

    rt_kprintf("PERIPH READ CHECKS COMPLETE passed=%u unavailable=%u\n", passed, unavailable);
    return RT_EOK;
}
MSH_CMD_EXPORT(ls2k3000_periph_test, Read RTC SPI JEDEC and SPD without changing persistent data);

struct periph_worker
{
    struct rt_semaphore *done;
    struct rt_spi_device *spi;
    struct rt_i2c_bus_device *i2c;
    rt_device_t rtc;
    unsigned int completed;
    unsigned int errors;
};

static void periph_worker_entry(void *argument)
{
    struct periph_worker *worker = argument;

    for (unsigned int round = 0; round < 32; ++round)
    {
        uint8_t opcode = 0x9f;
        uint8_t id[3];
        uint8_t offset = 0;
        uint8_t bytes[4];
        struct timeval time;
        struct rt_i2c_msg messages[2] = {
            {.addr = 0x50, .len = 1, .buf = &offset},
            {.addr = 0x50, .flags = RT_I2C_RD, .len = sizeof(bytes), .buf = bytes},
        };

        if (rt_spi_send_then_recv(worker->spi, &opcode, 1, id, sizeof(id)) ||
            !id[0] || id[0] == 0xff || rt_i2c_transfer(worker->i2c, messages, 2) != 2 ||
            rt_device_control(worker->rtc, RT_DEVICE_CTRL_RTC_GET_TIMEVAL, &time))
        {
            ++worker->errors;
        }

        ++worker->completed;
        rt_thread_mdelay(1);
    }

    rt_sem_release(worker->done);
}

static long ls2k3000_periph_stress(void)
{
    struct rt_semaphore done;
    struct periph_worker workers[4] = {0};
    struct rt_spi_configuration config = {
        .mode = RT_SPI_MSB | RT_SPI_MODE_0,
        .data_width = 8,
        .max_hz = 1000000,
    };
    struct rt_spi_device *spi = (struct rt_spi_device *)rt_device_find("spi0cs0");
    struct rt_i2c_bus_device *i2c = rt_i2c_bus_device_find("i2c0");
    rt_device_t rtc = rt_device_find("rtc");
    unsigned int started = 0;
    unsigned int completed = 0;
    unsigned int errors = 0;

    if (!spi || !i2c || !rtc || rt_spi_configure(spi, &config))
    {
        return -RT_ENOSYS;
    }

    if (rt_sem_init(&done, "periph-test", 0, RT_IPC_FLAG_PRIO))
    {
        return -RT_ERROR;
    }

    for (unsigned int i = 0; i < 4; ++i)
    {
        workers[i].done = &done;
        workers[i].spi = spi;
        workers[i].i2c = i2c;
        workers[i].rtc = rtc;
        rt_thread_t thread = rt_thread_create("periph-test", periph_worker_entry,
                                              &workers[i], 4096, 20, 5);

        if (!thread)
        {
            break;
        }

        if (rt_thread_startup(thread))
        {
            rt_thread_delete(thread);
            break;
        }

        ++started;
    }

    for (unsigned int i = 0; i < started; ++i)
    {
        rt_sem_take(&done, RT_WAITING_FOREVER);
    }

    for (unsigned int i = 0; i < started; ++i)
    {
        completed += workers[i].completed;
        errors += workers[i].errors;
    }

    rt_sem_detach(&done);
    rt_kprintf("PERIPH STRESS %s workers=%u rounds=%u errors=%u\n",
               started == 4 && completed == 128 && !errors ? "PASS" : "FAIL",
               started, completed, errors);
    return started == 4 && completed == 128 && !errors ? RT_EOK : -RT_ERROR;
}
MSH_CMD_EXPORT(ls2k3000_periph_stress, Concurrent read-only SPI I2C and RTC checks);
