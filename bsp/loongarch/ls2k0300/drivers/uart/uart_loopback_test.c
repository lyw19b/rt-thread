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
 *   LS2K0300 development BSP: uart loopback test.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#define DBG_TAG "uart.loopback"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 默认测试配置 */
#define UART_LOOPBACK_DEFAULT_DEV       "uart1"
#define UART_LOOPBACK_DEFAULT_BAUD      115200
#define UART_LOOPBACK_DEFAULT_DATA_BITS 8
#define UART_LOOPBACK_DEFAULT_STOP_BITS 1
#define UART_LOOPBACK_DEFAULT_PARITY    PARITY_NONE
#define UART_LOOPBACK_TEST_SIZE         256
#define UART_LOOPBACK_RX_TIMEOUT_MS     2000

/* 测试模式类型 */
#define PATTERN_INCREMENTAL 0   /* 0x00, 0x01, 0x02... */
#define PATTERN_DECREMENTAL 1   /* 0xFF, 0xFE, 0xFD... */
#define PATTERN_ALTERNATING 2   /* 0x55, 0xAA, 0x55... */
#define PATTERN_FIXED       3   /* 固定值 */
#define PATTERN_RANDOM      4   /* 随机数据 */

/**
 * @brief 生成测试数据模式
 * @param buf: 待填充的缓冲区
 * @param len: 缓冲区长度
 * @param pattern: 模式类型
 * @param fixed_val: PATTERN_FIXED 模式的固定值
 */
static void generate_test_pattern(rt_uint8_t *buf, rt_size_t len, int pattern, rt_uint8_t fixed_val)
{
    rt_size_t i;

    for (i = 0; i < len; i++)
    {
        switch (pattern)
        {
        case PATTERN_INCREMENTAL:
            buf[i] = (rt_uint8_t)(i & 0xFF);
            break;
        case PATTERN_DECREMENTAL:
            buf[i] = (rt_uint8_t)(0xFF - (i & 0xFF));
            break;
        case PATTERN_ALTERNATING:
            buf[i] = (i % 2 == 0) ? 0x55 : 0xAA;
            break;
        case PATTERN_FIXED:
            buf[i] = fixed_val;
            break;
        case PATTERN_RANDOM:
            buf[i] = (rt_uint8_t)(rand() & 0xFF);
            break;
        default:
            buf[i] = (rt_uint8_t)(i & 0xFF);
            break;
        }
    }
}

/**
 * @brief UART 回环测试
 * @param dev_name: UART 设备名 (如 "uart1")
 * @param baud_rate: 波特率
 * @param test_len: 测试数据长度
 * @param pattern: 测试模式类型
 * @return RT_EOK 成功，负数错误码失败
 */
static rt_err_t uart_loopback_test(const char *dev_name, rt_int32_t baud_rate, rt_size_t test_len,
                                   int pattern)
{
    rt_device_t uart_dev;
    struct serial_configure config;
    rt_uint8_t *tx_buf = RT_NULL;
    rt_uint8_t *rx_buf = RT_NULL;
    rt_size_t tx_pos = 0;
    rt_size_t rx_pos = 0;
    rt_err_t result = RT_EOK;
    rt_tick_t start_tick;
    rt_size_t error_count = 0;
    rt_size_t i;

    /* 分配缓冲区 */
    tx_buf = (rt_uint8_t *)rt_malloc(test_len);
    rx_buf = (rt_uint8_t *)rt_malloc(test_len);
    if (tx_buf == RT_NULL || rx_buf == RT_NULL)
    {
        LOG_E("Failed to allocate memory for test buffers");
        result = -RT_ENOMEM;
        goto _exit;
    }

    /* 生成测试数据模式 */
    generate_test_pattern(tx_buf, test_len, pattern, 0xA5);
    memset(rx_buf, 0, test_len);

    /* 查找 UART 设备 */
    uart_dev = rt_device_find(dev_name);
    if (uart_dev == RT_NULL)
    {
        LOG_E("Cannot find device: %s", dev_name);
        result = -RT_ERROR;
        goto _exit;
    }

    /* 打开 UART 设备（带中断接收标志） */
    result = rt_device_open(uart_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (result != RT_EOK)
    {
        LOG_E("Failed to open %s, result: %d", dev_name, result);
        goto _exit;
    }

    /* 配置 UART */
    config.baud_rate = baud_rate;
    config.data_bits = UART_LOOPBACK_DEFAULT_DATA_BITS;
    config.stop_bits = UART_LOOPBACK_DEFAULT_STOP_BITS;
    config.parity = UART_LOOPBACK_DEFAULT_PARITY;
    config.bit_order = BIT_ORDER_LSB;
    config.invert = NRZ_NORMAL;
    config.bufsz = RT_SERIAL_RB_BUFSZ;
    config.reserved = 0;

    result = rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (result != RT_EOK)
    {
        LOG_E("Failed to configure %s, result: %d", dev_name, result);
        goto _close_exit;
    }

    rt_kprintf("\n========== UART Loopback Test ==========\n");
    rt_kprintf("Device: %s\n", dev_name);
    rt_kprintf("Baud rate: %d\n", baud_rate);
    rt_kprintf("Data bits: %d, Stop bits: %d, Parity: %s\n", config.data_bits, config.stop_bits,
               config.parity == PARITY_NONE ? "None"
                                            : (config.parity == PARITY_EVEN ? "Even" : "Odd"));
    rt_kprintf("Test length: %d bytes\n", test_len);
    rt_kprintf("Pattern: %s\n", pattern == PATTERN_INCREMENTAL   ? "Incremental"
                                : pattern == PATTERN_DECREMENTAL ? "Decremental"
                                : pattern == PATTERN_ALTERNATING ? "Alternating(0x55/0xAA)"
                                : pattern == PATTERN_FIXED       ? "Fixed(0xA5)"
                                                                 : "Random");
    rt_kprintf("WARNING: Ensure TX and RX pins are physically connected!\n");
    rt_kprintf("========================================\n\n");

    /* 发送和接收数据 - 边发边收，防止缓冲区溢出 */
    start_tick = rt_tick_get();

    while (tx_pos < test_len || rx_pos < test_len)
    {
        /* 发送一小批数据（最多16字节），避免一次性填满缓冲区 */
        if (tx_pos < test_len)
        {
            rt_size_t tx_batch = (test_len - tx_pos > 16) ? 16 : (test_len - tx_pos);
            rt_size_t tx_size = rt_device_write(uart_dev, 0, &tx_buf[tx_pos], tx_batch);
            if (tx_size > 0)
            {
                tx_pos += tx_size;
            }
        }

        /* 立即读取接收数据 */
        if (rx_pos < test_len)
        {
            rt_size_t rx_size = rt_device_read(uart_dev, 0, &rx_buf[rx_pos], test_len - rx_pos);
            if (rx_size > 0)
            {
                rx_pos += rx_size;
            }
        }

        /* 检查超时 */
        if ((rt_tick_get() - start_tick) > rt_tick_from_millisecond(UART_LOOPBACK_RX_TIMEOUT_MS))
        {
            LOG_W("Receive timeout, received %d/%d bytes", rx_pos, test_len);
            break;
        }

        rt_thread_yield();
    }

    /* 比较收发数据 */
    rt_kprintf("Transmit: %d bytes\n", tx_pos);
    rt_kprintf("Receive:  %d bytes\n", rx_pos);

    if (rx_pos != test_len)
    {
        LOG_E("Data length mismatch!");
        result = -RT_ERROR;
    }

    /* 校验数据内容 */
    rt_size_t compare_len = (rx_pos < test_len) ? rx_pos : test_len;
    for (i = 0; i < compare_len; i++)
    {
        if (tx_buf[i] != rx_buf[i])
        {
            error_count++;
            if (error_count <= 5)
            {
                LOG_E("Data mismatch at byte %d: TX=0x%02X, RX=0x%02X", i, tx_buf[i], rx_buf[i]);
            }
        }
    }

    if (error_count > 5)
    {
        LOG_E("... and %d more errors", error_count - 5);
    }

    /* 打印测试结果 */
    rt_kprintf("\n========== Test Result ==========\n");
    if (result == RT_EOK && rx_pos == test_len && error_count == 0)
    {
        rt_kprintf("[PASS] All %d bytes transmitted and verified successfully!\n", test_len);
    }
    else
    {
        rt_kprintf("[FAIL] Errors: %d, Received: %d/%d bytes\n", error_count, rx_pos, test_len);
    }
    rt_kprintf("=================================\n");

_close_exit:
    rt_device_close(uart_dev);

_exit:
    if (tx_buf)
    {
        rt_free(tx_buf);
    }
    if (rx_buf)
    {
        rt_free(rx_buf);
    }

    return result;
}

/**
 * @brief UART 回环测试命令
 *
 * 用法: uart_loopback [dev] [baud] [len] [pattern]
 *   - dev:     串口设备名，默认: uart1
 *   - baud:    波特率，默认: 115200
 *   - len:     测试数据长度 (1-4096)，默认: 256 字节
 *   - pattern: 测试数据模式:
 *              0 = 递增模式 (0x00, 0x01, 0x02...)
 *              1 = 递减模式 (0xFF, 0xFE, 0xFD...)
 *              2 = 交替模式 (0x55, 0xAA, 0x55...)
 *              3 = 固定值 (0xA5)
 *              4 = 随机数据
 *
 * 示例:
 *   uart_loopback uart2 115200 256 0
 *   uart_loopback uart1 9600 100 2
 */
static int uart_loopback(int argc, char *argv[])
{
    const char *dev_name = UART_LOOPBACK_DEFAULT_DEV;
    rt_int32_t baud_rate = UART_LOOPBACK_DEFAULT_BAUD;
    rt_size_t test_len = UART_LOOPBACK_TEST_SIZE;
    int pattern = PATTERN_INCREMENTAL;

    /* 解析参数 */
    if (argc > 1)
    {
        dev_name = argv[1];
    }
    if (argc > 2)
    {
        baud_rate = atoi(argv[2]);
        if (baud_rate <= 0)
        {
            rt_kprintf("Error: Invalid baud rate: %s\n", argv[2]);
            return -RT_EINVAL;
        }
    }
    if (argc > 3)
    {
        test_len = atoi(argv[3]);
        if (test_len <= 0 || test_len > 4096)
        {
            rt_kprintf("Error: Invalid test length (1-4096): %s\n", argv[3]);
            return -RT_EINVAL;
        }
    }
    if (argc > 4)
    {
        pattern = atoi(argv[4]);
        if (pattern < 0 || pattern > 4)
        {
            rt_kprintf("Error: Invalid pattern (0-4): %s\n", argv[4]);
            return -RT_EINVAL;
        }
    }

    /* 运行测试 */
    uart_loopback_test(dev_name, baud_rate, test_len, pattern);

    return 0;
}

MSH_CMD_EXPORT(uart_loopback, UART loopback test(TX->RX short circuit required));

/**
 * @brief UART 回环压力测试命令
 *
 * 用法: uart_stress [dev] [baud] [iterations]
 *   - dev:        串口设备名，默认: uart1
 *   - baud:       波特率，默认: 115200
 *   - iterations: 测试次数 (1-1000)，默认: 10
 *
 * 该命令会执行多次回环测试:
 *   - 固定数据长度: 64 字节/次
 *   - 循环测试模式: 0->1->2->3->4->0... (全部 5 种模式)
 *
 * 示例:
 *   uart_stress                   (uart1 测试 10 次，波特率 115200)
 *   uart_stress uart2 9600        (uart2 测试 10 次，波特率 9600)
 *   uart_stress uart1 115200 100  (uart1 测试 100 次)
 */
static int uart_stress(int argc, char *argv[])
{
    const char *dev_name = (argc > 1) ? argv[1] : UART_LOOPBACK_DEFAULT_DEV;
    rt_int32_t baud_rate = (argc > 2) ? atoi(argv[2]) : UART_LOOPBACK_DEFAULT_BAUD;
    int iterations = (argc > 3) ? atoi(argv[3]) : 10;
    int pass_count = 0;
    int fail_count = 0;
    int i;
    rt_err_t result;

    if (iterations <= 0 || iterations > 1000)
    {
        rt_kprintf("Error: Invalid iterations (1-1000)\n");
        return -RT_EINVAL;
    }

    rt_kprintf("\n========== UART Stress Test ==========\n");
    rt_kprintf("Device: %s, Baud: %d, Iterations: %d\n", dev_name, baud_rate, iterations);
    rt_kprintf("WARNING: Ensure TX and RX pins are shorted!\n");
    rt_kprintf("=======================================\n\n");

    for (i = 0; i < iterations; i++)
    {
        int pattern = i % 5;  /* 循环遍历所有模式 */
        rt_kprintf("Test %d/%d (pattern %d)... ", i + 1, iterations, pattern);

        result = uart_loopback_test(dev_name, baud_rate, 64, pattern);

        if (result == RT_EOK)
        {
            rt_kprintf("PASS\n");
            pass_count++;
        }
        else
        {
            rt_kprintf("FAIL\n");
            fail_count++;
        }

        rt_thread_mdelay(100);  /* 测试间隔延时 */
    }

    rt_kprintf("\n========== Stress Test Summary ==========\n");
    rt_kprintf("Total:  %d\n", iterations);
    rt_kprintf("Passed: %d\n", pass_count);
    rt_kprintf("Failed: %d\n", fail_count);
    rt_kprintf("Success rate: %d%%\n", (pass_count * 100) / iterations);
    rt_kprintf("=========================================\n");

    return 0;
}

MSH_CMD_EXPORT(uart_stress, UART stress test(usage : uart_stress[dev][baud][iterations]));
