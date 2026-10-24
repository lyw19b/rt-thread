/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-24     OpenLANPU Contributors    retain SD filesystem round-trip test
 *
 */

/*
 * Description:
 *   Writes, synchronizes, closes, reopens and verifies a user-selected test file.
 *
 * Notes:
 *   Run on a mounted SD filesystem. O_EXCL prevents overwriting existing data.
 *   Successful tests remove their file; failures retain it for examination.
 *   Readback can use the filesystem cache; reboot/readback and card removal
 *   testing are separate hardware acceptance steps.
 *
 */

#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define SD_TEST_CHUNK 4096U

static unsigned char pattern(unsigned int position)
{
    return (position * 17U + (position >> 9) * 23U + (position >> 17)) & 255U;
}

static int sd_filetest(int argc, char **argv)
{
    unsigned char *buffer;
    unsigned int mib = 32, total, offset;
    int fd = -1, result = -1;
    char *end;

    if (argc < 2 || argc > 3)
    {
        rt_kprintf("usage: sd_filetest /sd/new-test-file [MiB:1..256]\n");
        return -1;
    }

    if (argc == 3)
    {
        unsigned long value = strtoul(argv[2], &end, 10);

        if (*end || end == argv[2] || value < 1 || value > 256)
        {
            return -1;
        }

        mib = value;
    }

    total = mib * 1024U * 1024U;
    buffer = rt_malloc(SD_TEST_CHUNK);
    if (!buffer)
    {
        return -1;
    }

    fd = open(argv[1], O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0)
    {
        rt_kprintf("cannot create new test file: %s\n", argv[1]);
        goto out;
    }

    for (offset = 0; offset < total; offset += SD_TEST_CHUNK)
    {
        for (unsigned int i = 0; i < SD_TEST_CHUNK; i++)
        {
            buffer[i] = pattern(offset + i);
        }

        if (write(fd, buffer, SD_TEST_CHUNK) != SD_TEST_CHUNK)
        {
            rt_kprintf("short/failed write at %u\n", offset);
            goto out;
        }
    }

    if (fsync(fd))
    {
        rt_kprintf("fsync failed\n");
        goto out;
    }

    result = close(fd);
    fd = -1;
    if (result)
    {
        goto out;
    }

    result = -1;
    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        goto out;
    }

    for (offset = 0; offset < total; offset += SD_TEST_CHUNK)
    {
        if (read(fd, buffer, SD_TEST_CHUNK) != SD_TEST_CHUNK)
        {
            rt_kprintf("short/failed read at %u\n", offset);
            goto out;
        }

        for (unsigned int i = 0; i < SD_TEST_CHUNK; i++)
        {
            if (buffer[i] != pattern(offset + i))
            {
                rt_kprintf("data mismatch at %u\n", offset + i);
                goto out;
            }
        }
    }

    if (read(fd, buffer, 1) != 0)
    {
        goto out;
    }

    result = close(fd);
    fd = -1;
    if (!result)
    {
        result = unlink(argv[1]);
    }

out:
    if (fd >= 0)
    {
        close(fd);
    }

    rt_free(buffer);
    rt_kprintf("sd_filetest %s: %u MiB\n", result ? "FAIL" : "PASS", mib);
    return result;
}
MSH_CMD_EXPORT(sd_filetest, Verify a new file on the mounted SD card);
