/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: uefi_entry.S
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Leave firmware with IRQs masked; no stack or memory access after DMW setup.
 */

    .section .text
    .balign 4
    .global ls2k3000_efi_enter
ls2k3000_efi_enter:
    li.d    $t0, 4
    csrxchg $zero, $t0, 0
    li.d    $t0, 0x8000000000000001
    csrwr   $t0, 0x180
    li.d    $t0, 0x9000000000000011
    csrwr   $t0, 0x181
    dbar    0
    ibar    0
    li.d    $a0, 0x4c53334b55454649
    li.d    $a1, 0x0fff0000
    move    $a2, $zero
    li.d    $t0, 0x9000000002000000
    jr      $t0

    .global ls2k3000_efi_stop
ls2k3000_efi_stop:
    li.d    $t0, 4
    csrxchg $zero, $t0, 0
1:
    idle    0
    b       1b
