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
 *   LS2K0300 development BSP: bsp dma map.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __BSP_DMA_MAP_H__
#define __BSP_DMA_MAP_H__

/*
 * 此库用于设置设备的DMA映射
 * 需要注意的是，难以避免有的设备之间共用了DMA通道
 * 比如 UART0 和 SPI2 的 DMA 通道 默认都是用了 0 1 通道
 * 那么就存在两种情况：
 * UART0 和 SPI2 在场景里面只会用到一个。
 * 那么就问题不大，正常用就好了。
 * 但是如果设想UART0和SPI2都要用，那么就需要 UART0 和 SPI2 用的时候不要冲突
 * 通常两者都要用的情况，那么建议就写成一个用一个不用。选占用IO时间长的那个就好。
 *
 * 下面的
 * 2x1 4x2 7x1 指代的是
 * 2x1 加密两通道 0 1 选择一种，由 AES DES SM4 SM3 使用
 * 4x2 相邻两通道 01 23 45 67 选择一种，由 UART I2C SPI I2S 使用
 * 7x1 7通道任选选择一种，由 ADC CAN 使用
 *
 * 对应设置函数，如果传入的 dev_dma_map 变量不在范围内，那么设置失败返回1，否则返回0
 *
 * dma_map_get_base 函数，只需要传入 dev_dma_map 变量，那么就会返回对应用到的通道
 * 2x1 返回的是 0 1
 * 4x2 返回的是 0 2 4 6 也就是相邻两通道的前一个 这样拿到返回值的时候 +1 就是第二个用到的通道
 * 7x1 返回的是 0到7
 *
 * 这个库是和 2k300 绑定的，如果要支持其他CPU，自行改变。
 */

/**
 * @brief DMA设备映射枚举
 * @details 定义不同外设的DMA通道映射类型，用于指定设备使用的DMA通道组
 */
enum dev_dma_map
{
    AES_W_DMA_MAP,       ///< AES加密模块写DMA映射
    AES_R_DMA_MAP,       ///< AES加密模块读DMA映射
    DES_W_DMA_MAP,       ///< DES加密模块写DMA映射
    DES_R_DMA_MAP,       ///< DES加密模块读DMA映射
    SM4_W_DMA_MAP,       ///< SM4加密模块写DMA映射
    SM4_R_DMA_MAP,       ///< SM4加密模块读DMA映射
    SM3_DMA_MAP,         ///< SM3加密模块DMA映射
    RES_1_DMA_MAP,       ///< 保留DMA映射1
    UART_0_DMA_MAP,      ///< UART0 DMA映射
    UART_1_DMA_MAP,      ///< UART1 DMA映射
    UART_2_DMA_MAP,      ///< UART2 DMA映射
    UART_3_DMA_MAP,      ///< UART3 DMA映射
    UART_4_DMA_MAP,      ///< UART4 DMA映射
    UART_5_DMA_MAP,      ///< UART5 DMA映射
    UART_6_DMA_MAP,      ///< UART6 DMA映射
    UART_7_DMA_MAP,      ///< UART7 DMA映射
    UART_8_DMA_MAP,      ///< UART8 DMA映射
    UART_9_DMA_MAP,      ///< UART9 DMA映射
    I2C_0_DMA_MAP,       ///< I2C0 DMA映射
    I2C_1_DMA_MAP,       ///< I2C1 DMA映射
    I2C_2_DMA_MAP,       ///< I2C2 DMA映射
    I2C_3_DMA_MAP,       ///< I2C3 DMA映射
    SPI_2_DMA_MAP,       ///< SPI2 DMA映射
    SPI_3_DMA_MAP,       ///< SPI3 DMA映射
    I2S_DMA_MAP,         ///< I2S DMA映射
    ADC_DMA_MAP,         ///< ADC DMA映射
    CAN_0_RX_DMA_MAP,    ///< CAN0接收DMA映射
    CAN_1_RX_DMA_MAP,    ///< CAN1接收DMA映射
    CAN_2_RX_DMA_MAP,    ///< CAN2接收DMA映射
    CAN_3_RX_DMA_MAP,    ///< CAN3接收DMA映射
};

/**
 * @brief 2x1类型DMA通道选择枚举
 * @details 用于加密模块(AES/DES/SM4/SM3)的DMA通道选择，共2个独立通道
 */
enum dev_dma_map_type_2x1
{
    crypto_dma_channel_0 = 0,  ///< 加密模块DMA通道0
    crypto_dma_channel_1,      ///< 加密模块DMA通道1
};

/**
 * @brief 4x2类型DMA通道选择枚举
 * @details 用于UART/I2C/SPI/I2S等外设的DMA通道选择，共4组相邻双通道
 */
enum dev_dma_map_type_4x2
{
    dma_channel_01 = 0,  ///< DMA通道组0-1
    dma_channel_23,      ///< DMA通道组2-3
    dma_channel_45,      ///< DMA通道组4-5
    dma_channel_67,      ///< DMA通道组6-7
};

/**
 * @brief 7x1类型DMA通道选择枚举
 * @details 用于ADC/CAN等外设的DMA通道选择，共8个独立通道
 */
enum dev_dma_map_type_7x1
{
    dma_channel_0 = 0,   ///< DMA通道0
    dma_channel_1,       ///< DMA通道1
    dma_channel_2,       ///< DMA通道2
    dma_channel_3,       ///< DMA通道3
    dma_channel_4,       ///< DMA通道4
    dma_channel_5,       ///< DMA通道5
    dma_channel_6,       ///< DMA通道6
    dma_channel_7,       ///< DMA通道7
};

/**
 * @brief 配置2x1类型设备的DMA通道
 * @param dev 设备DMA映射类型，来自enum dev_dma_map
 * @param map 通道选择，来自enum dev_dma_map_type_2x1
 * @return 0-配置成功，1-配置失败（参数无效）
 */
int dma_map_setup_type_2x1_channel(enum dev_dma_map dev, enum dev_dma_map_type_2x1 map);

/**
 * @brief 配置4x2类型设备的DMA通道组
 * @param dev 设备DMA映射类型，来自enum dev_dma_map
 * @param map 通道组选择，来自enum dev_dma_map_type_4x2
 * @return 0-配置成功，1-配置失败（参数无效）
 */
int dma_map_setup_type_4x2_channel(enum dev_dma_map dev, enum dev_dma_map_type_4x2 map);

/**
 * @brief 配置7x1类型设备的DMA通道
 * @param dev 设备DMA映射类型，来自enum dev_dma_map
 * @param map 通道选择，来自enum dev_dma_map_type_7x1
 * @return 0-配置成功，1-配置失败（参数无效）
 */
int dma_map_setup_type_7x1_channel(enum dev_dma_map dev, enum dev_dma_map_type_7x1 map);

/**
 * @brief 获取设备DMA通道的基地址
 * @param dev 设备DMA映射类型，来自enum dev_dma_map
 * @return 成功返回DMA通道基地址（对于4x2类型返回通道组起始地址），失败返回-1
 * @note 4x2类型通道组返回起始通道号，实际使用时+1可获取第二个通道
 */
int dma_map_get_base(enum dev_dma_map dev);

#endif
