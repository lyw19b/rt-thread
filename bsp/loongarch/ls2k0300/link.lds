/* SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: link.
 */

INCLUDE "link_kmd.lds"
MEMORY
{
    SRAM : ORIGIN = __kernel_virtual_base__ + 0x200000, LENGTH = 0x1000000
}
__SECONDARY_STACK_SIZE__ = 0;
INCLUDE "../common/link.lds"
