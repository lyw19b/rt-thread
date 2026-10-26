/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-26     OpenLANPU Contributors    exercise LA264 memory alignment cases
 *
 * Description:
 *   Verify byte offsets, short lengths, guards and overlapping memory moves.
 * Notes:
 *   Run as memory_align_test on the board, or compile with LS2K_MEMORY_HOST_TEST.
 *   Volatile function pointers prevent compiler builtins bypassing the functions.
 */

#include <stddef.h>
#include <string.h>

#ifdef LS2K_MEMORY_HOST_TEST
#include <stdio.h>
#define TEST_PRINT printf
#else
#include <rtthread.h>
#define TEST_PRINT rt_kprintf
#endif

#if defined(BSP_USING_SELFTEST) || defined(LS2K_MEMORY_HOST_TEST)

static void *(*volatile copy_memory)(void *, const void *, size_t) = memcpy;
static void *(*volatile move_memory)(void *, const void *, size_t) = memmove;
static void *(*volatile fill_memory)(void *, int, size_t) = memset;
static int (*volatile compare_memory)(const void *, const void *, size_t) = memcmp;

static int same_bytes(const unsigned char *left, const unsigned char *right, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (left[i] != right[i])
        {
            return 0;
        }
    }

    return 1;
}

static long memory_align_test(void)
{
    unsigned char source[320] __attribute__((aligned(16)));
    unsigned char actual[320] __attribute__((aligned(16)));
    unsigned char expected[320] __attribute__((aligned(16)));
    unsigned int cases = 0;

    for (size_t i = 0; i < sizeof(source); ++i)
    {
        source[i] = (unsigned char)(i * 37 + 11);
    }

    for (size_t size = 0; size <= 256; ++size)
    {
        for (unsigned int from = 0; from < 16; ++from)
        {
            for (unsigned int to = 0; to < 16; ++to)
            {
                for (size_t i = 0; i < sizeof(actual); ++i)
                {
                    actual[i] = expected[i] = 0xa5;
                }

                for (size_t i = 0; i < size; ++i)
                {
                    expected[16 + to + i] = source[16 + from + i];
                }

                if (copy_memory(actual + 16 + to, source + 16 + from, size) !=
                        actual + 16 + to || !same_bytes(actual, expected, sizeof(actual)) ||
                    compare_memory(actual + 16 + to, source + 16 + from, size) != 0)
                {
                    TEST_PRINT("MEMORY ALIGN: FAIL copy size=%u from=%u to=%u\n",
                               (unsigned int)size, from, to);
                    return -1;
                }

                ++cases;
            }
        }

        for (unsigned int to = 0; to < 16; ++to)
        {
            for (size_t i = 0; i < sizeof(actual); ++i)
            {
                actual[i] = expected[i] = 0xa5;
            }

            for (size_t i = 0; i < size; ++i)
            {
                expected[16 + to + i] = 0xe7;
            }

            if (fill_memory(actual + 16 + to, 0x1e7, size) != actual + 16 + to ||
                !same_bytes(actual, expected, sizeof(actual)))
            {
                TEST_PRINT("MEMORY ALIGN: FAIL fill size=%u offset=%u\n",
                           (unsigned int)size, to);
                return -1;
            }

            ++cases;
        }

        for (unsigned int from = 0; from < 32; ++from)
        {
            for (unsigned int to = 0; to < 32; ++to)
            {
                for (size_t i = 0; i < sizeof(actual); ++i)
                {
                    actual[i] = expected[i] = source[i];
                }

                for (size_t i = 0; i < size; ++i)
                {
                    expected[16 + to + i] = source[16 + from + i];
                }

                if (move_memory(actual + 16 + to, actual + 16 + from, size) !=
                        actual + 16 + to || !same_bytes(actual, expected, sizeof(actual)))
                {
                    TEST_PRINT("MEMORY ALIGN: FAIL move size=%u from=%u to=%u\n",
                               (unsigned int)size, from, to);
                    return -1;
                }

                ++cases;
            }
        }
    }

    actual[0] = 0x80;
    expected[0] = 0x7f;
    if (compare_memory(actual, expected, 1) <= 0 ||
        compare_memory(expected, actual, 1) >= 0 ||
        compare_memory(actual, expected, 0) != 0)
    {
        TEST_PRINT("MEMORY ALIGN: FAIL comparison ordering\n");
        return -1;
    }

    TEST_PRINT("MEMORY ALIGN: PASS (%u cases)\n", cases + 3);
    return 0;
}

#ifdef LS2K_MEMORY_HOST_TEST
int main(void)
{
    return memory_align_test() != 0;
}
#else
MSH_CMD_EXPORT(memory_align_test, Verify memory primitive alignment and overlap);
#endif

#endif
