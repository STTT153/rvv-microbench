    .section .text
    .align  2

    # Each baseline matches its corresponding indexed-load kernel except for
    # the omitted vluxei32.v instruction.

    .globl  baseline_kernel_m1
    .type   baseline_kernel_m1, @function
baseline_kernel_m1:
    vsetvli t0, a2, e32, m1, ta, ma
    vle32.v v0, (a1)
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v8, (a4)
    fence   rw, rw
    ret
    .size   baseline_kernel_m1, .-baseline_kernel_m1

    .globl  baseline_kernel_m2
    .type   baseline_kernel_m2, @function
baseline_kernel_m2:
    vsetvli t0, a2, e32, m2, ta, ma
    vle32.v v0, (a1)
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v8, (a4)
    fence   rw, rw
    ret
    .size   baseline_kernel_m2, .-baseline_kernel_m2

    .globl  baseline_kernel_m4
    .type   baseline_kernel_m4, @function
baseline_kernel_m4:
    vsetvli t0, a2, e32, m4, ta, ma
    vle32.v v0, (a1)
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v8, (a4)
    fence   rw, rw
    ret
    .size   baseline_kernel_m4, .-baseline_kernel_m4

    .globl  baseline_kernel_m8
    .type   baseline_kernel_m8, @function
baseline_kernel_m8:
    vsetvli t0, a2, e32, m8, ta, ma
    vle32.v v0, (a1)
1:
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v8, (a4)
    fence   rw, rw
    ret
    .size   baseline_kernel_m8, .-baseline_kernel_m8

    .section .note.GNU-stack,"",@progbits
