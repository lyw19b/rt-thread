/* SPDX-License-Identifier: Apache-2.0 */
.text
.global _start
_start:
    bl user_main
    ori $a0, $zero, 1
    ori $a7, $zero, 1
    syscall 0

/* A cloned child resumes with a different stack, so keep this in assembly. */
.global loongarch_test_clone
loongarch_test_clone:
    addi.d $sp, $sp, -16
    st.d $ra, $sp, 0
    ori $a7, $zero, 130
    syscall 0
    beqz $a0, 1f
    ld.d $ra, $sp, 0
    addi.d $sp, $sp, 16
    ret
1:
    bl clone_child
