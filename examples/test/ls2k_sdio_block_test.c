/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               retain read-only dual-host storage validation
 */

/*
 * Description:
 *   Read a named block device twice sequentially and sample the whole device.
 * Notes:
 *   No writes, formatting, mounting or sync commands are issued. The device
 *   must remain unchanged during the test. CRC32 uses the IEEE polynomial.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>

#define READ_BYTES 16384U

static rt_uint32_t block_crc(rt_uint32_t crc, const rt_uint8_t *bytes, rt_size_t size)
{
    for (rt_size_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];

        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }

    return crc;
}

static int sdio_block_test(int argc, char **argv)
{
    struct rt_device_blk_geometry geometry;
    rt_device_t device;
    rt_uint8_t *first;
    rt_uint8_t *second;
    rt_uint64_t limit;
    rt_uint64_t read_bytes = 0;
    rt_uint32_t expected = 0;
    rt_uint32_t seed = 0x2a3000;
    rt_tick_t start = rt_tick_get();
    unsigned int mib = argc > 2 ? atoi(argv[2]) : 16;
    rt_size_t count;
    int result = -RT_ERROR;

    if (argc < 2 || argc > 3 || !mib || mib > 256)
    {
        rt_kprintf("Usage: sdio_block_test device [MiB:1..256]\n");
        return -RT_EINVAL;
    }

    device = rt_device_find(argv[1]);

    if (!device || device->type != RT_Device_Class_Block ||
        rt_device_open(device, RT_DEVICE_OFLAG_RDONLY))
    {
        rt_kprintf("BLOCK UNAVAILABLE device=%s\n", argv[1]);
        return -RT_ENOSYS;
    }

    first = rt_malloc(READ_BYTES);
    second = rt_malloc(READ_BYTES);

    if (!first || !second ||
        rt_device_control(device, RT_DEVICE_CTRL_BLK_GETGEOME, &geometry) ||
        (geometry.bytes_per_sector != 512 && geometry.bytes_per_sector != 4096))
    {
        goto out;
    }

    count = READ_BYTES / geometry.bytes_per_sector;
    limit = (rt_uint64_t)mib * 1024U * 1024U / geometry.bytes_per_sector;

    if (geometry.sector_count < limit || geometry.sector_count > INT64_MAX)
    {
        goto out;
    }

    rt_kprintf("BLOCK device=%s sectors=%llu sector_bytes=%u MiB=%u\n",
               argv[1], geometry.sector_count, geometry.bytes_per_sector, mib);

    for (unsigned int pass = 0; pass < 2; ++pass)
    {
        rt_uint32_t crc = UINT32_MAX;

        for (rt_uint64_t sector = 0; sector < limit; sector += count)
        {
            if (rt_device_read(device, sector, first, count) != (rt_ssize_t)count)
            {
                rt_kprintf("BLOCK read failed sector=%llu pass=%u\n", sector, pass);
                goto out;
            }

            crc = block_crc(crc, first, READ_BYTES);
            read_bytes += READ_BYTES;

            if ((sector + count) * geometry.bytes_per_sector == 1024U * 1024U)
            {
                rt_kprintf("BLOCK first_MiB_crc32=%08x pass=%u\n", ~crc, pass);
            }
        }

        if (pass && expected != ~crc)
        {
            rt_kprintf("BLOCK sequential mismatch expected=%08x actual=%08x\n",
                       expected, ~crc);
            goto out;
        }

        expected = ~crc;
        rt_kprintf("BLOCK sequential pass=%u crc32=%08x\n", pass, expected);
    }

    for (unsigned int sample = 0; sample < 128; ++sample)
    {
        rt_uint64_t sector;

        seed = seed * 1664525U + 1013904223U;
        sector = (rt_uint64_t)seed % (geometry.sector_count - count + 1);

        if (sample == 0)
        {
            sector = geometry.sector_count - count;
        }

        if (rt_device_read(device, sector, first, count) != (rt_ssize_t)count ||
            rt_device_read(device, sector, second, count) != (rt_ssize_t)count ||
            rt_memcmp(first, second, READ_BYTES))
        {
            rt_kprintf("BLOCK random mismatch sample=%u sector=%llu\n", sample, sector);
            goto out;
        }

        read_bytes += 2 * READ_BYTES;
    }

    result = RT_EOK;

out:
    rt_free(second);
    rt_free(first);
    rt_device_close(device);
    rt_kprintf("BLOCK %s device=%s bytes=%llu ticks=%u\n",
               result ? "FAIL" : "PASS", argv[1], read_bytes, rt_tick_get() - start);
    return result;
}

/* FinSH exports argument-taking commands through its generic syscall slot. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
MSH_CMD_EXPORT(sdio_block_test, Read-only sequential and random block verification);
#pragma GCC diagnostic pop
