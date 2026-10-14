/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#ifdef RT_USING_DFS
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <errno.h>
#include "stack.h"

/**
 * @brief    Adapt the kernel toolchain libc file operations to RT-Thread DFS.
 *
 * @note     Only kernel-mode callers use these Linux syscall numbers. Smart user
 *           calls retain the official RT-Thread numbering. Buffer pointers are trusted
 *           kernel addresses, and DFS operations may sleep.
 *
 * @param    regs is the kernel-mode syscall frame containing a7 and its arguments.
 *
 * @return   The operation result, a negative errno, or -ENOSYS for unsupported calls.
 */
rt_base_t loongarch_kernel_syscall(struct pt_regs *regs)
{
    rt_base_t result;
    switch (regs->r_a7)
    {
    case SYS_openat:
        if ((int)regs->r_a0 != AT_FDCWD)
            return -ENOSYS;
        result = open((const char *)regs->r_a1, regs->r_a2, regs->r_a3);
        break;
    case SYS_close:
        result = close(regs->r_a0);
        break;
    case SYS_read:
        result = read(regs->r_a0, (void *)regs->r_a1, regs->r_a2);
        break;
    case SYS_write:
        result = write(regs->r_a0, (void *)regs->r_a1, regs->r_a2);
        break;
    case SYS_writev:
    case SYS_readv:
    {
        const struct iovec *iov = (void *)regs->r_a1;
        rt_size_t i;
        result = 0;
        for (i = 0; i < regs->r_a2; i++)
        {
            ssize_t count = regs->r_a7 == SYS_writev ?
                write(regs->r_a0, iov[i].iov_base, iov[i].iov_len) :
                read(regs->r_a0, iov[i].iov_base, iov[i].iov_len);
            if (count < 0)
                return result ? result : -errno;
            result += count;
            if ((size_t)count != iov[i].iov_len)
                break;
        }
        break;
    }
    case SYS_lseek:
        result = lseek(regs->r_a0, regs->r_a1, regs->r_a2);
        break;
    case SYS_ioctl:
        result = ioctl(regs->r_a0, regs->r_a1, (void *)regs->r_a2);
        break;
    case SYS_fcntl:
        result = fcntl(regs->r_a0, regs->r_a1, regs->r_a2);
        break;
    default:
        return -ENOSYS;
    }
    return result < 0 ? -errno : result;
}
#endif
