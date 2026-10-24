/* SPDX-License-Identifier: Apache-2.0 */
INCLUDE "link_kmd.lds"
MEMORY
{
    SRAM : ORIGIN = __kernel_virtual_base__ + 0x200000, LENGTH = 0x1000000
}
__SECONDARY_STACK_SIZE__ = 0;
INCLUDE "../common/link.lds"
