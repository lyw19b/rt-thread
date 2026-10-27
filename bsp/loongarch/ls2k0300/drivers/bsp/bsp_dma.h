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
 *   LS2K0300 development BSP: bsp dma.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#ifndef __BSP_DMA_H__
#define __BSP_DMA_H__

/**
 * @brief DMA传输方向定义
 * @note DMA_DIR_READ_P: 从外设地址读取数据
 *       DMA_DIR_READ_M: 从内存地址读取数据
 */
#define DMA_DIR_READ_P 0
#define DMA_DIR_READ_M 1

/**
 * @brief DMA数据传输大小定义
 * @note DMA_SIZE_8: 8位数据传输
 *       DMA_SIZE_16: 16位数据传输
 *       DMA_SIZE_32: 32位数据传输
 */
#define DMA_SIZE_8  0
#define DMA_SIZE_16 1
#define DMA_SIZE_32 2

/**
 * @brief DMA通道优先级定义
 * @note 数字越大优先级越高，范围从0到3
 */
#define DMA_PRIORITY_0 0
#define DMA_PRIORITY_1 1
#define DMA_PRIORITY_2 2
#define DMA_PRIORITY_3 3

/**
 * @brief DMA中断类型定义
 * @note DMA_TRANSFER_END: 传输完成中断
 *       DMA_TRANSFER_HALF: 半传输完成中断
 *       DMA_TRANSFER_ERROR: 传输错误中断
 */
#define DMA_TRANSFER_END   0
#define DMA_TRANSFER_HALF  1
#define DMA_TRANSFER_ERROR 2

/**
 * @brief DMA传输状态定义
 * @note DMA_STATUS_FINISH: 传输完成
 *       DMA_STATUS_FAILED: 传输失败
 *       DMA_STATUS_RUNNING: 传输中
 */
#define DMA_STATUS_FINISH  0
#define DMA_STATUS_FAILED  1
#define DMA_STATUS_RUNNING 2

/**
 * @brief DMA传输信息结构体前置声明
 */
struct dma_transfer_info;

/**
 * @brief DMA中断处理函数类型定义
 * @param info DMA传输信息结构体指针
 * @param type 中断类型，取值为DMA_TRANSFER_xxx宏定义
 */
typedef void (*dma_interrupt_handler_t)(struct dma_transfer_info *, int);

/**
 * @brief DMA传输信息结构体
 * @details 包含DMA传输的所有配置参数和状态信息
 */
typedef struct dma_transfer_info
{
    unsigned char dma_channel;      ///< DMA通道编号
    unsigned char dir;              ///< 传输方向，取值为DMA_DIR_READ_P或DMA_DIR_READ_M
    unsigned char circ;             ///< 是否循环传输：1-循环模式，0-单次模式
    unsigned char p_inc;            ///< 外设地址自增：1-使能，0-禁止
    unsigned char m_inc;            ///< 内存地址自增：1-使能，0-禁止
    unsigned char p_size;           ///< 外设数据宽度，取值为DMA_SIZE_xxx宏定义
    unsigned char m_size;           ///< 内存数据宽度，取值为DMA_SIZE_xxx宏定义
    unsigned char dma_priority;     ///< 通道优先级，取值为DMA_PRIORITY_xxx宏定义
    unsigned char mem_2_mem;        ///< 内存到内存传输：1-使能，0-禁止

    /*
     * size： 传输大小
     * 注意这个和 p_size m_size 有关，通常这两个是一样的
     * 比如 DMA_SIZE_8 那么就代表一次传输8bit
     * 如果size为5，代表这次传输 40 bit，也就是5字节
     * 如果 DMA_SIZE_32 那么就代表一次传输32bit
     * 如果size为5，代表这次传输 160 bit，也就是20字节
     * 如果  p_size m_size 不一致，那么按大的那个值来算
     */
    unsigned int size;

    /*
     * p_addr 是外设地址
     * m_addr 是内存地址
     * DMA传输需要的是物理地址
     * 但是为了方便，设置这两个变量时可以不考虑如何把地址变成物理地址
     * 使用 dma_config dma_transfer dma_reconfig_m 等配置DMA的接口时
     * 里面会自行转换
     */
    unsigned long long p_addr;
    unsigned long long m_addr;

    /**
     * DMA传输完成回调函数
     * @note 如果为NULL，则不使能中断
     */
    dma_interrupt_handler_t dma_callback;
    void *dma_callback_param;       ///< 回调函数参数
} dma_transfer_info;

/**
 * @brief 初始化DMA传输信息结构体
 * @param info 指向dma_transfer_info结构体的指针
 */
void dma_transfer_info_init(dma_transfer_info *info);

/**
 * @brief 配置DMA通道
 * @param info 指向dma_transfer_info结构体的指针
 * @return 0-配置成功，非0-配置失败
 * @note 需要与dma_start配合使用启动传输
 */
int dma_config(dma_transfer_info *info);

/**
 * @brief 启动DMA传输
 * @param info 指向dma_transfer_info结构体的指针
 * @return 0-启动成功，非0-启动失败
 */
int dma_start(dma_transfer_info *info);

/**
 * @brief 配置并启动DMA传输
 * @param info 指向dma_transfer_info结构体的指针
 * @return 0-成功，非0-失败
 * @note 该函数内部会依次调用dma_config和dma_start
 */
int dma_transfer(dma_transfer_info *info);

/**
 * @brief 停止DMA传输
 * @param info 指向dma_transfer_info结构体的指针
 */
void dma_stop(dma_transfer_info *info);

/**
 * @brief 检查DMA传输是否完成
 * @param info 指向dma_transfer_info结构体的指针
 * @return DMA_STATUS_FINISH-已完成，DMA_STATUS_RUNNING-传输中，DMA_STATUS_FAILED-传输失败
 */
int dma_check_transfer_end(dma_transfer_info *info);

/**
 * @brief 重新配置DMA内存参数并启动传输
 * @param info 指向dma_transfer_info结构体的指针
 * @return 0-成功，非0-失败
 */
int dma_reconfig_m(dma_transfer_info *info);

#endif
