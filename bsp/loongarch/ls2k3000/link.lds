/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: link.lds
 * Author: LoongsonLab
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               add LS2K3000 Explorer support
 */

/*
 * Description:
 *   Keep the kernel inside memory explicitly allocated by the EFI loader.
 */

MEMORY
{
    SRAM : ORIGIN = 0x9000000002000000, LENGTH = 0x04000000
}
__SECONDARY_STACK_SIZE__ = 0;
INCLUDE "../common/link.lds"
ASSERT(_end < 0x9000000006000000, "LS2K3000 kernel exceeds reserved image space")
