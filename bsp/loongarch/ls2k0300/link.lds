/* SPDX-License-Identifier: Apache-2.0 */
MEMORY
{
    SRAM : ORIGIN = 0x9000000000200000, LENGTH = 0x1000000
}
__SECONDARY_STACK_SIZE__ = 0;
INCLUDE "../common/link.lds"
