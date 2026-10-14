/* SPDX-License-Identifier: Apache-2.0 */
MEMORY
{
    SRAM : ORIGIN = 0x9000000098000000, LENGTH = 0x1000000
}
__SECONDARY_STACK_SIZE__ = __STACKSIZE__;
INCLUDE "../common/link.lds"
