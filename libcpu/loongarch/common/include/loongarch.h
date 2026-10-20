/*
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Description: LoongArch privileged register access interfaces.
 *              Detailed documents access website:
 *              https://loongson.cn
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-10     LoongsonLab  the first version
 * 2025-03-13     LoongsonLab  Add CSR operation
 * 2025-03-13     LoongsonLab  Add TLB Refill operations
 */

#ifndef _ASM_LOONGARCH_H
#define _ASM_LOONGARCH_H

#include "loongarch_regs.h"

#ifndef __ASSEMBLY__
#include <larchintrin.h>

#define read_cpucfg(reg) __cpucfg(reg)

/* CSR */
#define csr_read32(reg)            __csrrd_w(reg)
#define csr_read64(reg)            __csrrd_d(reg)
#define csr_write32(val, reg)      __csrwr_w(val, reg)
#define csr_write64(val, reg)      __csrwr_d(val, reg)
#define csr_xchg32(val, mask, reg) __csrxchg_w(val, mask, reg)
#define csr_xchg64(val, mask, reg) __csrxchg_d(val, mask, reg)

/* IOCSR */
#define iocsr_read32(reg)       __iocsrrd_w(reg)
#define iocsr_read64(reg)       __iocsrrd_d(reg)
#define iocsr_write32(val, reg) __iocsrwr_w(val, reg)
#define iocsr_write64(val, reg) __iocsrwr_d(val, reg)


/* Named accessors keep immediate CSR operands inside the compiler intrinsics.
 * Writes return the previous CSR value, matching csr_write32/64. */
#define LOONGARCH_CSR_ACCESSORS(name, reg, width, type) \
    static inline type read_csr_##name(void)            \
    {                                                   \
        return csr_read##width(reg);                    \
    }                                                   \
    static inline type write_csr_##name(type value)     \
    {                                                   \
        return csr_write##width(value, reg);            \
    }

LOONGARCH_CSR_ACCESSORS(asid, LOONGARCH_CSR_ASID, 32, unsigned int)
LOONGARCH_CSR_ACCESSORS(entryhi, LOONGARCH_CSR_TLBEHI, 64, unsigned long)
LOONGARCH_CSR_ACCESSORS(entrylo0, LOONGARCH_CSR_TLBELO0, 64, unsigned long)
LOONGARCH_CSR_ACCESSORS(entrylo1, LOONGARCH_CSR_TLBELO1, 64, unsigned long)
LOONGARCH_CSR_ACCESSORS(ecfg, LOONGARCH_CSR_ECFG, 32, unsigned int)
LOONGARCH_CSR_ACCESSORS(estat, LOONGARCH_CSR_ESTAT, 32, unsigned int)
LOONGARCH_CSR_ACCESSORS(tlbidx, LOONGARCH_CSR_TLBIDX, 32, unsigned int)
LOONGARCH_CSR_ACCESSORS(euen, LOONGARCH_CSR_EUEN, 32, unsigned int)
LOONGARCH_CSR_ACCESSORS(stlbpgsize, LOONGARCH_CSR_STLBPGSIZE, 32, unsigned int)

#undef LOONGARCH_CSR_ACCESSORS

#define read_csr_cpuid()         csr_read32(LOONGARCH_CSR_CPUID)
#define read_csr_prcfg1()        csr_read64(LOONGARCH_CSR_PRCFG1)
#define write_csr_prcfg1(val)    csr_write64(val, LOONGARCH_CSR_PRCFG1)
#define read_csr_prcfg2()        csr_read64(LOONGARCH_CSR_PRCFG2)
#define write_csr_prcfg2(val)    csr_write64(val, LOONGARCH_CSR_PRCFG2)
#define read_csr_prcfg3()        csr_read64(LOONGARCH_CSR_PRCFG3)
#define write_csr_prcfg3(val)    csr_write64(val, LOONGARCH_CSR_PRCFG3)
#define read_csr_rvacfg()        csr_read32(LOONGARCH_CSR_RVACFG)
#define write_csr_rvacfg(val)    csr_write32(val, LOONGARCH_CSR_RVACFG)
#define write_csr_tintclear(val) csr_write32(val, LOONGARCH_CSR_TINTCLR)
#define read_csr_impctl1()       csr_read64(LOONGARCH_CSR_IMPCTL1)
#define write_csr_impctl1(val)   csr_write64(val, LOONGARCH_CSR_IMPCTL1)
#define write_csr_impctl2(val)   csr_write64(val, LOONGARCH_CSR_IMPCTL2)

/**
 * @brief Update ECFG bits and return the previous value.
 * @note The caller serializes local ECFG updates, as with the original helpers.
 */
static inline unsigned long update_csr_ecfg(unsigned long clear, unsigned long set)
{
    unsigned long previous = read_csr_ecfg();

    write_csr_ecfg((previous & ~clear) | set);
    return previous;
}

static inline unsigned long set_csr_ecfg(unsigned long set)
{
    return update_csr_ecfg(0, set);
}

static inline unsigned long clear_csr_ecfg(unsigned long clear)
{
    return update_csr_ecfg(clear, 0);
}

static inline void write_csr_pagesize(unsigned int size)
{
    csr_xchg32(size << CSR_TLBIDX_SIZE, CSR_TLBIDX_SIZEM, LOONGARCH_CSR_TLBIDX);
}

static inline void write_csr_tlbrefill_pagesize(unsigned int size)
{
    csr_xchg64(size << CSR_TLBREHI_PS_SHIFT, CSR_TLBREHI_PS, LOONGARCH_CSR_TLBREHI);
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_LOONGARCH_H */
