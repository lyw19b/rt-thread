/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               isolate adapter model from RT runtime
 */

#ifndef LS2K_SDIO_TEST_STUBS_H
#define LS2K_SDIO_TEST_STUBS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BSP_USING_SDIO
#ifndef SDIO_TEST_SINGLE
#define BSP_SDIO_DUAL_HOST
#else
#define BSP_SDIO_PORT 1
#endif
#define BSP_SDIO_ROOT_CLOCK_HZ 200000000U
#define BSP_SDIO_MAX_CLOCK_HZ 25000000U
#define RT_USING_FINSH
#define RT_NAME_MAX 16
#define RT_TICK_PER_SECOND 1000
#define RT_IPC_FLAG_PRIO 0
#define RT_EOK 0
#define RT_ERROR 1
#define RT_ETIMEOUT 110
#define RT_EINVAL 22
#define RT_EIO 5
#define RT_ENOSYS 38
#define RT_EPERM 1
#define RT_EBUSY 16
#define RT_ENOMEM 12
#define RT_UNUSED(value) (void)(value)
#define INIT_DEVICE_EXPORT(function) \
    static void *const init_##function __attribute__((used)) = (void *)&function
#define MSH_CMD_EXPORT(function, description) \
    static void *const export_##function __attribute__((used)) = (void *)&function
#define DBG_INFO 1
#define LOG_E(...) \
    do \
    { \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } while (0)
#define LOG_I(...) \
    do \
    { \
        printf(__VA_ARGS__); \
        putchar('\n'); \
    } while (0)
#define rt_memcpy memcpy
#define rt_memset memset
#define rt_snprintf snprintf
#define rt_kprintf printf
#define rt_tick_from_millisecond(value) (value)
#define LS2K0300_CLOCK_SAMPLE 0x16000404UL
#define LS2K0300_CLOCK_SCALE 0x16000420UL
#define LS2K0300_DDR_PLL0 0x16000410UL
#define LS2K0300_DDR_PLL1 0x16000414UL
#define LS2K0300_DMA_LIMIT 0x10000000UL
#define LS2K_UNCACHED(value) ((uintptr_t)(value))
#define LS2K_PHYSICAL(value) sdio_test_physical((uintptr_t)(value))
#define HWREG32(address) (*sdio_test_register(address))
#define ls2k0300_apb_clock(sample, pll0, pll1, bit) \
    ((void)(sample), (void)(pll0), (void)(pll1), (void)(bit), 200000000U)
#define RESP_NONE 0
#define RESP_R1 1
#define RESP_R1B 2
#define RESP_R2 3
#define RESP_R3 4
#define RESP_R4 5
#define resp_type(command) ((command)->flags)
#define SEND_OP_COND 1
#define SD_IO_SEND_OP_COND 5
#define SEND_EXT_CSD 8
#define APP_CMD 55
#define DATA_DIR_READ 1
#define DATA_DIR_WRITE 2
#define DATA_STREAM 4
#define MMCSD_BUS_WIDTH_1 0
#define MMCSD_BUS_WIDTH_4 2
#define MMCSD_BUS_WIDTH_8 3
#define MMCSD_POWER_OFF 0
#define MMCSD_POWER_UP 1
#define VDD_32_33 (1U << 20)
#define VDD_33_34 (1U << 21)
#define MMCSD_BUSWIDTH_4 1
#define MMCSD_BUSWIDTH_8 2
#define MMCSD_MUTBLKWRITE 4
#define MMCSD_SUP_NONREMOVABLE 8
#define MMCSD_SUP_HIGHSPEED 16

typedef uintptr_t rt_ubase_t;
typedef intptr_t rt_base_t;
typedef uint32_t rt_uint32_t;
typedef int32_t rt_int32_t;
struct rt_semaphore {unsigned int unused;};
struct rt_spinlock {unsigned int unused;};
struct rt_mmcsd_data
{
    void *buf;
    unsigned int sg_len, flags, blks, blksize, bytes_xfered;
    int err;
};
struct rt_mmcsd_cmd
{
    unsigned int cmd_code, arg, flags, busy_timeout;
    uint32_t resp[4];
    struct rt_mmcsd_data *data;
    int err;
};
struct rt_mmcsd_req
{
    struct rt_mmcsd_cmd *sbc, *cmd, *stop;
    struct rt_mmcsd_data *data;
};
struct rt_mmcsd_io_cfg {unsigned int bus_width, power_mode, clock;};
struct rt_mmcsd_card {uint16_t rca;};
struct rt_mmcsd_host;
struct rt_mmcsd_host_ops
{
    void (*request)(struct rt_mmcsd_host *, struct rt_mmcsd_req *);
    void (*set_iocfg)(struct rt_mmcsd_host *, struct rt_mmcsd_io_cfg *);
    rt_int32_t (*get_card_status)(struct rt_mmcsd_host *);
};
struct rt_mmcsd_host
{
    char name[16];
    void *private_data;
    const struct rt_mmcsd_host_ops *ops;
    struct rt_mmcsd_card *card;
    uint32_t freq_min, freq_max, valid_ocr, flags;
    uint32_t max_seg_size, max_dma_segs, max_blk_size, max_blk_count;
};

uint32_t *sdio_test_register(uintptr_t address);
uintptr_t sdio_test_physical(uintptr_t pointer);
uint32_t rt_tick_get(void);
void rt_thread_mdelay(unsigned int milliseconds);
int rt_sem_take(struct rt_semaphore *, unsigned int);
struct rt_mmcsd_host *mmcsd_alloc_host(void);
void mmcsd_change(struct rt_mmcsd_host *host);

static inline void rt_hw_dmb(void)
{
}

static inline void rt_spin_lock_init(struct rt_spinlock *lock)
{
    (void)lock;
}

static inline rt_base_t rt_spin_lock_irqsave(struct rt_spinlock *lock)
{
    (void)lock;
    return 0;
}

static inline void rt_spin_unlock_irqrestore(struct rt_spinlock *lock, rt_base_t level)
{
    (void)lock;
    (void)level;
}
static inline void rt_sem_init(struct rt_semaphore *sem, const char *name, int count, int flags)
{
    (void)sem;
    (void)name;
    (void)count;
    (void)flags;
}
static inline void rt_sem_release(struct rt_semaphore *sem)
{
    (void)sem;
}

static inline void rt_sem_detach(struct rt_semaphore *sem)
{
    (void)sem;
}

static inline void mmcsd_req_complete(struct rt_mmcsd_host *host)
{
    (void)host;
}

static inline void mmcsd_host_lock(struct rt_mmcsd_host *host)
{
    (void)host;
}

static inline void mmcsd_host_unlock(struct rt_mmcsd_host *host)
{
    (void)host;
}

static inline void rt_hw_interrupt_mask(int irq)
{
    (void)irq;
}

static inline void rt_hw_interrupt_umask(int irq)
{
    (void)irq;
}

static inline void rt_hw_interrupt_install(int irq, void (*handler)(int, void *), void *arg, const char *name)
{
    (void)irq;
    (void)handler;
    (void)arg;
    (void)name;
}

#endif
